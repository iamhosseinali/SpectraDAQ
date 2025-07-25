#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "CustomCommandDialog.h"
#include <QHostAddress>
#include <QMessageBox>
#include <QDebug>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QValueAxis>
#include <limits>
#include <QtEndian>
#include <QRegularExpression>
#include <cmath>
#include <algorithm> // For std::minmax_element
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QInputDialog>
#include <QScrollArea>
#include <QGroupBox>
#include <QFormLayout>
#include <QPushButton>
#include <QSpinBox>
#include <QLabel>
#include <QHBoxLayout>
#include "LoggingManager.h"
#include <QInputDialog>
#include <QFileDialog>
#include "FieldDef.h"
#include "UdpWorker.h"
#include <QThread>

QT_CHARTS_USE_NAMESPACE

const quint8 FS_COMM_IDF = 0x55;
const quint8 FRQ_COMM_IDF = 0xAA;

// Simple C struct parser
QList<FieldDef> parseCStruct(const QString &structText) {
    QList<FieldDef> fields;
    QStringList lines = structText.split('\n');
    QRegularExpression re(R"((\w+_t|\w+)\s+(\w+)(\[(\d+)\])?;)");
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("//") || trimmed.startsWith("typedef") || trimmed.startsWith("{") || trimmed.startsWith("}"))
            continue;
        QRegularExpressionMatch match = re.match(trimmed);
        if (match.hasMatch()) {
            FieldDef field;
            field.type = match.captured(1);
            field.name = match.captured(2);
            field.count = match.captured(4).isEmpty() ? 1 : match.captured(4).toInt();
            fields.append(field);
        }
    }
    return fields;
}

// Helper to swap endianness for various types
#include <algorithm>
template<typename T>
T swapEndian(T u) {
    static_assert (CHAR_BIT == 8, "CHAR_BIT != 8");
    union {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;
    source.u = u;
    for (size_t k = 0; k < sizeof(T); k++)
        dest.u8[k] = source.u8[sizeof(T) - k - 1];
    return dest.u;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , autoScaleYTimer(new QTimer(this))
    , plotUpdateTimer(new QTimer(this))
{
    ui->setupUi(this);
    // Set stretch factors for main layout: chartView gets most space
    ui->verticalLayout->setStretch(0, 5); // chartView
    ui->verticalLayout->setStretch(1, 0); // inputLayout
    ui->verticalLayout->setStretch(2, 0); // settingsLayout
    ui->verticalLayout->setStretch(3, 0); // structTextEdit
    ui->verticalLayout->setStretch(4, 0); // fieldTableWidget

    // Allow large values for structCountSpinBox
    ui->structCountSpinBox->setMaximum(65536);
    ui->packetLengthSpinBox->setReadOnly(true);

    // Connect structCountSpinBox to update packet length when changed
    connect(ui->structCountSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int){
        // Recalculate packet length as if struct was just parsed
        QString structText = ui->structTextEdit->toPlainText();
        QList<FieldDef> fields = parseCStruct(structText);
        auto typeSize = [](const QString &type) -> int {
            if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
            if (type == "int16_t" || type == "uint16_t") return 2;
            if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
            if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
            return 0;
        };
        int structSize = 0;
        for (const FieldDef &field : fields) {
            int sz = typeSize(field.type);
            if (sz == 0) continue;
            structSize += sz * field.count;
        }
        int structCount = ui->structCountSpinBox->value();
        int totalSize = structSize * structCount;
        if (totalSize > 0) {
            ui->packetLengthSpinBox->setValue(totalSize);
        }
    });

    // Connect the button
    connect(ui->parseStructButton, &QPushButton::clicked, this, &MainWindow::on_parseStructButton_clicked);

    // Connect signals
    connect(ui->ipLineEdit, &QLineEdit::editingFinished,
            this, &MainWindow::on_ipLineEdit_editingFinished);
    connect(ui->portSpinBox, &QSpinBox::editingFinished,
            this, &MainWindow::on_portSpinBox_editingFinished);

    // Initialize with default values
    daqPort = ui->portSpinBox->value();
    // Remove old UDP socket and timer setup related to udpSocket and readPendingDatagrams
    // Remove all references to udpSocket, initializeSocket, and readPendingDatagrams
    // Remove connect(udpSocket, &QUdpSocket::readyRead, ...)
    // Remove MainWindow::readPendingDatagrams definition

    // Chart setup
    QChart *chart = new QChart();
    chart->setTitle("Real Time Graph");
    ui->chartView->setChart(chart);
    ui->chartView->setRenderHint(QPainter::Antialiasing);

    QLineSeries *series = new QLineSeries();
    series->setColor(Qt::red); // Add this line
    chart->addSeries(series);

    QValueAxis *axisX = new QValueAxis();
    QValueAxis *axisY = new QValueAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    axisX->setRange(0, 255);
    axisY->setRange(-150, 150); // Better for ±128 values

    connect(ui->fieldTableWidget, &QTableWidget::itemChanged,
            this, &MainWindow::on_fieldTableWidget_itemChanged);
    connect(ui->applyFftCheckBox, &QCheckBox::stateChanged, this, &MainWindow::on_applyFftCheckBox_stateChanged);
    // Disconnect valueChanged, connect editingFinished for FFT Length
    connect(ui->fftLengthSpinBox, &QSpinBox::editingFinished, this, &MainWindow::on_fftLengthSpinBox_editingFinished);
    // Set initial enabled state for FFT Length spin box
    ui->fftLengthSpinBox->setEnabled(!ui->applyFftCheckBox->isChecked());

    // Add buffer for time series
    valueHistory.clear();
    maxHistory = 256;

    // Set up sliders
    ui->xDivSlider->setMinimum(10);
    ui->xDivSlider->setMaximum(10000); // Limit X-Div to 10000
    ui->xDivSlider->setValue(256);

    ui->yDivSlider->setMinimum(10);
    ui->yDivSlider->setMaximum(100000);
    ui->yDivSlider->setValue(30000);

    // Connect sliders to slots
    connect(ui->xDivSlider, &QSlider::valueChanged, this, [this](int value){
        xDiv = value;
        maxHistory = xDiv;
        // Update X axis immediately
        QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
        if (axisX) axisX->setRange(0, xDiv - 1);
    });
    connect(ui->yDivSlider, &QSlider::valueChanged, this, [this](int value){
        yDiv = value;
        // Update Y axis immediately
        QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
        if (axisY) axisY->setRange(0, yDiv);
    });

    // Throttle auto Y-scaling: update every 100ms
    autoScaleYTimer->setInterval(100);
    connect(autoScaleYTimer, &QTimer::timeout, this, [this]() {
        if (ui->autoScaleYCheckBox->isChecked() && !valueHistory.isEmpty()) {
            QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
            auto minmax = std::minmax_element(valueHistory.begin(), valueHistory.end(),
                [](const QPointF& a, const QPointF& b) { return a.y() < b.y(); });
            float minVal = minmax.first->y();
            float maxVal = minmax.second->y();
            if (minVal == maxVal) {
                minVal -= 1.0f;
                maxVal += 1.0f;
            }
            if (axisY) axisY->setRange(minVal, maxVal);
        }
    });
    connect(ui->autoScaleYCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            autoScaleYTimer->start();
        } else {
            autoScaleYTimer->stop();
            // Set Y axis to manual value immediately
            QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
            if (axisY) axisY->setRange(0, yDiv);
        }
    });

    // Throttle plot updates: update every 30ms
    int refreshHz = ui->refreshRateSpinBox->value();
    int refreshInterval = 1000 / std::max(1, refreshHz);
    plotUpdateTimer->setInterval(refreshInterval);
    connect(plotUpdateTimer, &QTimer::timeout, this, &MainWindow::updatePlot);
    plotUpdateTimer->start();
    connect(ui->refreshRateSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int hz){
        int interval = 1000 / std::max(1, hz);
        plotUpdateTimer->setInterval(interval);
        qDebug() << "[UI] plotUpdateTimer interval set to" << interval << "ms for refresh rate" << hz;
    });

    updatePresetComboBox();
    updateCustomCommandsUI();
    connect(ui->logToCsvButton, &QPushButton::clicked, this, &MainWindow::on_logToCsvButton_clicked);

    // --- UDP Worker Thread Setup ---
    udpThread = new QThread(this);
    udpWorker = new UdpWorker();
    udpWorker->moveToThread(udpThread);
    connect(udpThread, &QThread::finished, udpWorker, &QObject::deleteLater);
    connect(this, &MainWindow::startUdp, udpWorker, &UdpWorker::start);
    connect(this, &MainWindow::stopUdp, udpWorker, &UdpWorker::stop);
    connect(this, &MainWindow::updateUdpConfig, udpWorker, &UdpWorker::updateConfig);
    connect(udpWorker, &UdpWorker::dataReceived, this, &MainWindow::handleUdpData, Qt::QueuedConnection);
    connect(this, &MainWindow::sendCustomDatagram, udpWorker, &UdpWorker::sendDatagram);
    udpThread->start();
    emit startUdp(ui->portSpinBox->value());

    // Connect debugLogCheckBox toggled signal
    connect(ui->debugLogCheckBox, &QCheckBox::toggled, this, [](bool checked){
        MainWindow::setDebugLogEnabled(checked);
    });
}

MainWindow::~MainWindow()
{
    emit stopUdp();
    if (udpThread) {
        udpThread->quit();
        udpThread->wait();
    }
    delete ui;
    autoScaleYTimer->stop();
}

void MainWindow::on_ipLineEdit_editingFinished()
{
    daqAddress = QHostAddress(ui->ipLineEdit->text());
    if (daqAddress.isNull()) {
        ui->statusbar->showMessage("Invalid IP address", 3000);
    } else {
        ui->statusbar->showMessage(tr("DAQ IP set to %1").arg(daqAddress.toString()), 3000);
    }
}

void MainWindow::on_portSpinBox_editingFinished()
{
    daqPort = static_cast<quint16>(ui->portSpinBox->value());
    // Remove all calls to initializeSocket(); from this file
}

void MainWindow::sendCommand(quint8 commandId, quint32 value)
{
    // Always update daqPort and daqAddress from UI before sending
    daqPort = static_cast<quint16>(ui->portSpinBox->value());
    daqAddress = QHostAddress(ui->ipLineEdit->text());
    if (daqAddress.isNull()) {
        QMessageBox::warning(this, "Error", "Set DAQ IP first!");
        return;
    }

    QByteArray command;
    command.append(commandId);
    quint32 leValue = qToLittleEndian(value);
    command.append(reinterpret_cast<const char*>(&leValue), sizeof(leValue));

    // udpSocket->writeDatagram(command, daqAddress, daqPort); // This line is removed
    ui->statusbar->showMessage(tr("Command %1 sent with value %2")
        .arg(commandId)
        .arg(value), 3000);
}

void MainWindow::on_parseStructButton_clicked()
{
    QString structText = ui->structTextEdit->toPlainText();
    QList<FieldDef> fields = parseCStruct(structText);

    // Helper: map type string to size in bytes
    auto typeSize = [](const QString &type) -> int {
        if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
        if (type == "int16_t" || type == "uint16_t") return 2;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        return 0; // Unknown type
    };

    // Print parsed fields and sizes
    qDebug() << "Parsed struct fields:";
    int structSize = 0;
    for (const FieldDef &field : fields) {
        int sz = typeSize(field.type);
        qDebug() << field.type << field.name << "count:" << field.count << "size:" << sz * field.count;
        if (sz == 0) continue; // skip unknown types
        structSize += sz * field.count;
    }
    int structCount = ui->structCountSpinBox->value();
    int totalSize = structSize * structCount;
    if (totalSize > 0) {
        ui->packetLengthSpinBox->setValue(totalSize);
    }

    // Display in table
    ui->fieldTableWidget->clear();
    ui->fieldTableWidget->setColumnCount(4);
    ui->fieldTableWidget->setHorizontalHeaderLabels({"Real Time Graph", "Type", "Name", "Count"});
    ui->fieldTableWidget->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i) {
        // Checkbox item
        QTableWidgetItem *checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(Qt::Unchecked);
        ui->fieldTableWidget->setItem(i, 0, checkItem);

        // Other columns
        ui->fieldTableWidget->setItem(i, 1, new QTableWidgetItem(fields[i].type));
        ui->fieldTableWidget->setItem(i, 2, new QTableWidgetItem(fields[i].name));
        ui->fieldTableWidget->setItem(i, 3, new QTableWidgetItem(QString::number(fields[i].count)));
    }
    ui->fieldTableWidget->resizeColumnsToContents();
    // After updating the table, emit updateUdpConfig
    int selectedField = -1;
    int selectedArrayIndex = 0;
    int selectedFieldCount = 1;
    for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
        QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedField = row;
            selectedFieldCount = ui->fieldTableWidget->item(row, 3)->text().toInt();
            break;
        }
    }
    if (selectedField >= 0 && selectedFieldCount > 1) {
        selectedArrayIndex = ui->arrayIndexSpinBox->value();
    }
    bool endianness = ui->endiannessCheckBox->isChecked();
    emit updateUdpConfig(structText, fields, structSize, endianness, selectedField, selectedArrayIndex, selectedFieldCount);
}

// Add FFT helper (Cooley-Tukey, radix-2, real input)
std::vector<float> MainWindow::computeFft(const std::vector<float> &data) {
    int N = data.size();
    std::vector<std::complex<float>> X(N);
    for (int i = 0; i < N; ++i) X[i] = data[i];
    // Bit reversal
    int j = 0;
    for (int i = 0; i < N; ++i) {
        if (i < j) std::swap(X[i], X[j]);
        int m = N >> 1;
        while (m >= 1 && j >= m) { j -= m; m >>= 1; }
        j += m;
    }
    // FFT
    for (int s = 1; (1 << s) <= N; ++s) {
        int m = 1 << s;
        std::complex<float> wm = std::exp(std::complex<float>(0, -2.0f * M_PI / m));
        for (int k = 0; k < N; k += m) {
            std::complex<float> w = 1;
            for (int l = 0; l < m / 2; ++l) {
                auto t = w * X[k + l + m / 2];
                auto u = X[k + l];
                X[k + l] = u + t;
                X[k + l + m / 2] = u - t;
                w *= wm;
            }
        }
    }
    // Return magnitude spectrum (first N/2)
    std::vector<float> mag(N / 2);
    for (int i = 0; i < N / 2; ++i) mag[i] = std::abs(X[i]);
    return mag;
}

void MainWindow::on_applyFftCheckBox_stateChanged(int state) {
    fftBuffer.clear();
    // Enable/disable FFT Length spin box based on Apply FFT state
    ui->fftLengthSpinBox->setEnabled(state != Qt::Checked);
    
    if (state == Qt::Checked) {
        // Clear time-domain data
        valueHistory.clear();
        sampleIndex = 0;
        
        // Reset chart for FFT display
        auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
        series->clear();
        
        // Configure axes for FFT
        QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
        QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
        
        if (axisX) {
            axisX->setTitleText("Frequency Bin");
            axisX->setRange(0, ui->fftLengthSpinBox->value()/2 - 1);
        }
        
        if (axisY) {
            axisY->setTitleText("Magnitude");
            axisY->setLabelFormat("%.1e");  // Scientific notation
        }
    } else {
        // Reset to time-domain configuration
        ui->chartView->chart()->setTitle("Real Time Graph");
        
        QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
        if (axisX) {
            axisX->setTitleText("Samples");
            axisX->setRange(0, xDiv - 1);
        }
        
        QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
        if (axisY) {
            axisY->setTitleText("Value");
            axisY->setLabelFormat("%.2f");  // Float format
            axisY->setRange(-yDiv, yDiv);
        }
    }
}
// Add new slot for editingFinished
void MainWindow::on_fftLengthSpinBox_editingFinished() {
    int value = ui->fftLengthSpinBox->value();
    // Snap to nearest power of 2 if not already
    int pow2 = 1;
    while (pow2 < value) pow2 <<= 1;
    int lower = pow2 >> 1;
    int nearest = (value - lower < pow2 - value) ? lower : pow2;
    if (value != nearest && nearest >= ui->fftLengthSpinBox->minimum() && nearest <= ui->fftLengthSpinBox->maximum()) {
        ui->fftLengthSpinBox->blockSignals(true);
        ui->fftLengthSpinBox->setValue(nearest);
        ui->fftLengthSpinBox->blockSignals(false);
        value = nearest;
    }
    fftBuffer.clear();
    if (ui->applyFftCheckBox->isChecked()) {
        // Reset FFT display
        auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
        series->clear();
        QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
        if (axisX) {
            axisX->setRange(0, value/2 - 1);
        }
    }
}

void MainWindow::plotFftData(const std::vector<float> &fftResult) {
    QVector<QPointF> points;
    for (int i = 0; i < (int)fftResult.size(); ++i) {
        points.append(QPointF(i, fftResult[i]));
    }
    
    auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
    series->replace(points);
    
    QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
    QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
    
    if (axisX) {
        axisX->setTitleText("Frequency Bin");
        axisX->setRange(0, points.size() - 1);
    }
    
    if (axisY) {
        axisY->setTitleText("Magnitude");
        float minVal = *std::min_element(fftResult.begin(), fftResult.end());
        float maxVal = *std::max_element(fftResult.begin(), fftResult.end());
        
        // Add 10% headroom for better visualization
        axisY->setRange(minVal, maxVal * 1.1);
        
        // Always use logarithmic scale for FFT
        axisY->setLabelFormat("%.1e");
    }
    
    // Set chart title for FFT mode
    ui->chartView->chart()->setTitle("FFT Magnitude Spectrum");
}

void MainWindow::processFftAndPlot() {
    if ((int)fftBuffer.size() == ui->fftLengthSpinBox->value()) {
        auto fftResult = computeFft(fftBuffer);
        plotFftData(fftResult);
        fftBuffer.clear();
    }
}

// Helper: calculate struct size from parsed struct
int MainWindow::getStructSize() {
    QString structText = ui->structTextEdit->toPlainText();
    QList<FieldDef> fields = parseCStruct(structText);
    auto typeSize = [](const QString &type) -> int {
        if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
        if (type == "int16_t" || type == "uint16_t") return 2;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        return 0;
    };
    int structSize = 0;
    for (const FieldDef &field : fields) {
        int sz = typeSize(field.type);
        if (sz == 0) continue;
        structSize += sz * field.count;
    }
    return structSize;
}

void MainWindow::on_fieldTableWidget_itemChanged(QTableWidgetItem *item)
{
    if (item->column() == 0 && item->checkState() == Qt::Checked) {
        ui->fieldTableWidget->blockSignals(true);
        for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
            if (row != item->row()) {
                QTableWidgetItem *otherItem = ui->fieldTableWidget->item(row, 0);
                if (otherItem && otherItem->checkState() == Qt::Checked) {
                    otherItem->setCheckState(Qt::Unchecked);
                }
            }
        }
        ui->fieldTableWidget->blockSignals(false);

        // Show/hide array index spinbox if selected field is an array
        int selectedRow = item->row();
        QString type = ui->fieldTableWidget->item(selectedRow, 1)->text();
        int count = ui->fieldTableWidget->item(selectedRow, 3)->text().toInt();
        if (count > 1) {
            ui->label_arrayIndex->setVisible(true);
            ui->arrayIndexSpinBox->setVisible(true);
            ui->arrayIndexSpinBox->setMinimum(0);
            ui->arrayIndexSpinBox->setMaximum(count - 1);
        } else {
            ui->label_arrayIndex->setVisible(false);
            ui->arrayIndexSpinBox->setVisible(false);
        }
        // Reset sample index and value history when changing field
        sampleIndex = 0;
        valueHistory.clear();
        // Emit updateUdpConfig
        QString structText = ui->structTextEdit->toPlainText();
        QList<FieldDef> fields = parseCStruct(structText);
        int structSize = 0;
        auto typeSize = [](const QString &type) -> int {
            if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
            if (type == "int16_t" || type == "uint16_t") return 2;
            if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
            if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
            return 0;
        };
        for (const FieldDef &field : fields) {
            int sz = typeSize(field.type);
            if (sz == 0) continue;
            structSize += sz * field.count;
        }
        int selectedField = selectedRow;
        int selectedArrayIndex = 0;
        int selectedFieldCount = count;
        if (selectedFieldCount > 1) {
            selectedArrayIndex = ui->arrayIndexSpinBox->value();
        }
        bool endianness = ui->endiannessCheckBox->isChecked();
        emit updateUdpConfig(structText, fields, structSize, endianness, selectedField, selectedArrayIndex, selectedFieldCount);
    }
}

// Throttled plot update
void MainWindow::updatePlot() {
    if (ui->applyFftCheckBox->isChecked()) {
        // FFT mode - do nothing (handled by processFftAndPlot)
        return;
    }

    // Time-domain mode
    auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
    if (valueHistory.isEmpty()) return;
    
    series->replace(valueHistory);
    
    QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
    if (axisX) {
        double minX = std::max(0.0, valueHistory.last().x() - xDiv + 1);
        double maxX = valueHistory.last().x();
        axisX->setRange(minX, maxX);
    }
    
    QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
    if (axisY && !ui->autoScaleYCheckBox->isChecked()) {
        axisY->setRange(-yDiv, yDiv);
    }
}

void MainWindow::handleUdpData(QVector<float> values) {
    // Check if a field is selected
    int selectedField = -1;
    for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
        QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedField = row;
            break;
        }
    }
    if (selectedField == -1) {
        if (ui->debugLogCheckBox->isChecked()) qDebug() << "[handleUdpData] No field selected, clearing plot.";
        valueHistory.clear();
        auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
        series->clear();
        return;
    }
    if (ui->debugLogCheckBox->isChecked()) qDebug() << "[handleUdpData] Received values for field" << selectedField << ":" << values;
    for (float value : values) {
        if (valueHistory.size() >= xDiv) valueHistory.pop_front();
        valueHistory.append(QPointF(sampleIndex++, value));
    }
    // Do not call updatePlot() here; let plotUpdateTimer control refresh
}

// Helper: collect all UI state into a QJsonObject
QJsonObject MainWindow::collectPreset() const {
    QJsonObject preset;
    preset["daq_ip"] = ui->ipLineEdit->text();
    preset["daq_port"] = ui->portSpinBox->value();
    preset["struct_def"] = ui->structTextEdit->toPlainText();
    preset["fft_length"] = ui->fftLengthSpinBox->value();
    preset["apply_fft"] = ui->applyFftCheckBox->isChecked();
    preset["x_div"] = ui->xDivSlider->value();
    preset["y_div"] = ui->yDivSlider->value();
    preset["refresh_rate"] = ui->refreshRateSpinBox->value();
    preset["endianness"] = ui->endiannessCheckBox->isChecked();
    preset["debug_log"] = ui->debugLogCheckBox->isChecked();
    preset["auto_scale_y"] = ui->autoScaleYCheckBox->isChecked();
    preset["selected_field"] = ui->fieldTableWidget->currentRow();
    preset["array_index"] = ui->arrayIndexSpinBox->value();
    preset["structs_per_packet"] = ui->structCountSpinBox->value();
    return preset;
}

// Helper: apply a QJsonObject preset to the UI
void MainWindow::applyPreset(const QJsonObject &preset) {
    if (preset.contains("daq_ip")) ui->ipLineEdit->setText(preset["daq_ip"].toString());
    if (preset.contains("daq_port")) ui->portSpinBox->setValue(preset["daq_port"].toInt());
    if (preset.contains("struct_def")) ui->structTextEdit->setPlainText(preset["struct_def"].toString());
    if (preset.contains("fft_length")) ui->fftLengthSpinBox->setValue(preset["fft_length"].toInt());
    if (preset.contains("apply_fft")) ui->applyFftCheckBox->setChecked(preset["apply_fft"].toBool());
    if (preset.contains("x_div")) ui->xDivSlider->setValue(preset["x_div"].toInt());
    if (preset.contains("y_div")) ui->yDivSlider->setValue(preset["y_div"].toInt());
    if (preset.contains("refresh_rate")) ui->refreshRateSpinBox->setValue(preset["refresh_rate"].toInt());
    if (preset.contains("endianness")) ui->endiannessCheckBox->setChecked(preset["endianness"].toBool());
    if (preset.contains("debug_log")) ui->debugLogCheckBox->setChecked(preset["debug_log"].toBool());
    if (preset.contains("auto_scale_y")) ui->autoScaleYCheckBox->setChecked(preset["auto_scale_y"].toBool());
    if (preset.contains("selected_field")) {
        int row = preset["selected_field"].toInt();
        if (row >= 0 && row < ui->fieldTableWidget->rowCount()) {
            ui->fieldTableWidget->setCurrentCell(row, 0);
            QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
            if (item) item->setCheckState(Qt::Checked);
        }
    }
    if (preset.contains("array_index")) ui->arrayIndexSpinBox->setValue(preset["array_index"].toInt());
    if (preset.contains("structs_per_packet")) ui->structCountSpinBox->setValue(preset["structs_per_packet"].toInt());
}

// Helper: update the preset combo box from file
void MainWindow::updatePresetComboBox() {
    QFile file("presets.json");
    if (!file.open(QIODevice::ReadOnly)) {
        ui->presetComboBox->clear();
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonArray presets = root["presets"].toArray();
    ui->presetComboBox->blockSignals(true);
    ui->presetComboBox->clear();
    for (const QJsonValue &val : presets) {
        QJsonObject obj = val.toObject();
        ui->presetComboBox->addItem(obj["name"].toString());
    }
    ui->presetComboBox->blockSignals(false);
}

// Save preset to file
void MainWindow::savePresetToFile(const QString &name) {
    QFile file("presets.json");
    QJsonDocument doc;
    QJsonObject root;
    QJsonArray presets;
    if (file.open(QIODevice::ReadOnly)) {
        doc = QJsonDocument::fromJson(file.readAll());
        root = doc.object();
        presets = root["presets"].toArray();
        file.close();
    }
    // Overwrite if exists
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].toObject()["name"].toString() == name) {
            presets.removeAt(i);
            break;
        }
    }
    QJsonObject preset = collectPreset();
    preset["name"] = name;
    presets.append(preset);
    root["presets"] = presets;
    doc.setObject(root);
    file.setFileName("presets.json");
    file.open(QIODevice::WriteOnly);
    file.write(doc.toJson());
    file.close();
    updatePresetComboBox();
}

// Load preset from file
void MainWindow::loadPresetFromFile(const QString &name) {
    QFile file("presets.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonArray presets = root["presets"].toArray();
    for (const QJsonValue &val : presets) {
        QJsonObject obj = val.toObject();
        if (obj["name"].toString() == name) {
            // Parse struct before applying preset fields
            if (obj.contains("struct_def")) {
                ui->structTextEdit->setPlainText(obj["struct_def"].toString());
                on_parseStructButton_clicked();
            }
            applyPreset(obj);
            break;
        }
    }
}

// Delete preset from file
void MainWindow::deletePresetFromFile(const QString &name) {
    QFile file("presets.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonArray presets = root["presets"].toArray();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].toObject()["name"].toString() == name) {
            presets.removeAt(i);
            break;
        }
    }
    root["presets"] = presets;
    doc.setObject(root);
    file.setFileName("presets.json");
    file.open(QIODevice::WriteOnly);
    file.write(doc.toJson());
    file.close();
    updatePresetComboBox();
}

// UI slots
void MainWindow::on_savePresetButton_clicked() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "Save Preset", "Preset name:", QLineEdit::Normal, "", &ok);
    if (ok && !name.trimmed().isEmpty()) {
        savePresetToFile(name.trimmed());
    }
}
void MainWindow::on_loadPresetButton_clicked() {
    QString name = ui->presetComboBox->currentText();
    if (!name.isEmpty()) {
        loadPresetFromFile(name);
    }
}
void MainWindow::on_deletePresetButton_clicked() {
    QString name = ui->presetComboBox->currentText();
    if (!name.isEmpty()) {
        deletePresetFromFile(name);
    }
}
void MainWindow::on_presetComboBox_currentIndexChanged(int index) {
    // Optionally auto-load preset on selection
    // QString name = ui->presetComboBox->itemText(index);
    // if (!name.isEmpty()) loadPresetFromFile(name);
    updateCustomCommandsUI();
}

void MainWindow::on_editCommandsButton_clicked() {
    // Find current preset
    QString name = ui->presetComboBox->currentText();
    if (name.isEmpty()) return;
    QFile file("presets.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonArray presets = root["presets"].toArray();
    for (int i = 0; i < presets.size(); ++i) {
        QJsonObject obj = presets[i].toObject();
        if (obj["name"].toString() == name) {
            QJsonArray commands = obj["custom_commands"].toArray();
            CustomCommandDialog dlg(commands, this);
            if (dlg.exec() == QDialog::Accepted) {
                obj["custom_commands"] = dlg.getCommands();
                presets[i] = obj;
                root["presets"] = presets;
                QFile outFile("presets.json");
                if (outFile.open(QIODevice::WriteOnly)) {
                    QJsonDocument outDoc(root);
                    outFile.write(outDoc.toJson());
                    outFile.close();
                }
                // Update UI with new commands
                updateCustomCommandsUI();
            }
            break;
        }
    }
}

void MainWindow::updateCustomCommandsUI() {
    // Remove old widget if present
    if (customCommandsWidget) {
        ui->verticalLayout->removeWidget(customCommandsWidget);
        customCommandsWidget->deleteLater();
        customCommandsWidget = nullptr;
    }
    // Find current preset
    QString name = ui->presetComboBox->currentText();
    if (name.isEmpty()) return;
    QFile file("presets.json");
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();
    QJsonArray presets = root["presets"].toArray();
    QJsonArray commands;
    for (int i = 0; i < presets.size(); ++i) {
        QJsonObject obj = presets[i].toObject();
        if (obj["name"].toString() == name) {
            commands = obj["custom_commands"].toArray();
            break;
        }
    }
    if (commands.isEmpty()) return;
    // Create a group box for commands
    QGroupBox *group = new QGroupBox("Custom Commands", this);
    QFormLayout *form = new QFormLayout(group);
    for (const QJsonValue &val : commands) {
        QJsonObject cmd = val.toObject();
        QString type = cmd["type"].toString();
        QString label = cmd["name"].toString();
        if (type == "spinbox") {
            QSpinBox *spin = new QSpinBox(group);
            spin->setMinimum(0);
            int valueSize = cmd["value_size"].toInt();
            if (valueSize < 1) valueSize = 1;
            if (valueSize > 64) valueSize = 64;
            // Set maximum for up to 8 bytes, otherwise clamp to max for QSpinBox
            if (valueSize <= 8) {
                quint64 maxVal = (valueSize == 8) ? std::numeric_limits<quint64>::max() : (quint64(1) << (8 * valueSize)) - 1;
                if (maxVal > quint64(std::numeric_limits<int>::max()))
                    maxVal = std::numeric_limits<int>::max();
                spin->setMaximum(static_cast<int>(maxVal));
            } else {
                spin->setMaximum(std::numeric_limits<int>::max());
            }
            QPushButton *setBtn = new QPushButton("Set", group);
            QWidget *rowWidget = new QWidget(group);
            QHBoxLayout *rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->addWidget(spin);
            rowLayout->addWidget(setBtn);
            rowLayout->setContentsMargins(0,0,0,0);
            rowWidget->setLayout(rowLayout);
            form->addRow(label, rowWidget);
            // Connect setBtn to send the command
            connect(setBtn, &QPushButton::clicked, this, [this, cmd, spin, valueSize]() {
                qDebug() << "Custom spinbox Set button pressed for" << cmd["name"].toString();
                QByteArray ba;
                // Header
                bool ok = false;
                uint32_t header = cmd["header"].toString().toUInt(&ok, 16);
                if (header != 0) {
                    for (int i = 3; i >= 0; --i) ba.append((header >> (8*i)) & 0xFF);
                }
                // Value (support up to 64 bytes, little-endian)
                int value = spin->value();
                QByteArray valueBytes;
                for (int i = valueSize-1; i >= 0; --i) valueBytes.append((value >> (8*i)) & 0xFF);
                // Swap endianness if requested (reverse valueBytes)
                if (cmd.contains("swap_endian") && cmd["swap_endian"].toBool() && valueBytes.size() > 1) {
                    std::reverse(valueBytes.begin(), valueBytes.end());
                }
                ba.append(valueBytes);
                // Trailer
                uint32_t trailer = cmd["trailer"].toString().toUInt(&ok, 16);
                if (trailer != 0) {
                    for (int i = 3; i >= 0; --i) ba.append((trailer >> (8*i)) & 0xFF);
                }
                // Always fetch current IP and port from UI
                QHostAddress addr(ui->ipLineEdit->text());
                quint16 port = static_cast<quint16>(ui->portSpinBox->value());
                if (!addr.isNull()) {
                    qDebug() << "daqAddress:" << addr << "daqPort:" << port;
                    qDebug() << "Custom spinbox command send:" << ba.toHex();
                    if (ui->debugLogCheckBox->isChecked()) qDebug() << "[MainWindow] Emitting sendCustomDatagram" << ba.toHex() << addr << port;
                    emit sendCustomDatagram(ba, addr, port);
                }
            });
        } else if (type == "button") {
            QPushButton *sendBtn = new QPushButton("Send", group);
            form->addRow(label, sendBtn);
            connect(sendBtn, &QPushButton::clicked, this, [this, cmd]() {
                qDebug() << "Custom Send button pressed for" << cmd["name"].toString();
                QByteArray ba;
                QString val = cmd["command"].toString();
                if (val.startsWith("0x")) {
                    bool ok = false;
                    uint32_t hex = val.toUInt(&ok, 16);
                    for (int i = 3; i >= 0; --i) ba.append((hex >> (8*i)) & 0xFF);
                } else {
                    ba = val.toUtf8();
                }
                // Always fetch current IP and port from UI
                QHostAddress addr(ui->ipLineEdit->text());
                quint16 port = static_cast<quint16>(ui->portSpinBox->value());
                if (!addr.isNull()) {
                    qDebug() << "daqAddress:" << addr << "daqPort:" << port;
                    qDebug() << "Custom button command send:" << ba.toHex();
                    if (ui->debugLogCheckBox->isChecked()) qDebug() << "[MainWindow] Emitting sendCustomDatagram" << ba.toHex() << addr << port;
                    emit sendCustomDatagram(ba, addr, port);
                }
            });
        }
    }
    group->setLayout(form);
    customCommandsWidget = group;
    ui->verticalLayout->addWidget(customCommandsWidget);
}

void MainWindow::on_logToCsvButton_clicked() {
    ui->logToCsvButton->setEnabled(false);
    ui->logToCsvButton->clearFocus();
    QInputDialog durationDialog(this);
    durationDialog.setWindowTitle("Log Duration");
    durationDialog.setLabelText("Enter duration (seconds):");
    durationDialog.setInputMode(QInputDialog::IntInput);
    durationDialog.setIntRange(1, 3600);
    durationDialog.setIntValue(10);
    durationDialog.setIntStep(1);
    if (durationDialog.exec() != QDialog::Accepted) {
        ui->logToCsvButton->setEnabled(true);
        return;
    }
    int duration = durationDialog.intValue();
    durationDialog.close();

    QFileDialog fileDialog(this);
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setDefaultSuffix("csv");
    fileDialog.setNameFilter("CSV Files (*.csv)");
    if (fileDialog.exec() != QDialog::Accepted) {
        ui->logToCsvButton->setEnabled(true);
        return;
    }
    QString filename = fileDialog.selectedFiles().first();
    fileDialog.close();

    QString structText = ui->structTextEdit->toPlainText();
    QList<FieldDef> fields = parseCStruct(structText);
    int structSize = getStructSize();
    if (fields.isEmpty() || structSize == 0) {
        QMessageBox::warning(this, "Error", "Invalid struct definition");
        ui->logToCsvButton->setEnabled(true);
        return;
    }

    if (autoScaleYTimer) autoScaleYTimer->stop();
    if (plotUpdateTimer) plotUpdateTimer->stop();
    for (auto w : findChildren<QWidget*>()) w->setEnabled(false);
    ui->statusbar->showMessage("Logging in progress...", 0);

    // Start logging in the worker thread
    QMetaObject::invokeMethod(udpWorker, "startLogging", Qt::QueuedConnection,
        Q_ARG(QList<FieldDef>, fields),
        Q_ARG(int, structSize),
        Q_ARG(int, duration),
        Q_ARG(QString, filename));
    connect(udpWorker, &UdpWorker::loggingFinished, this, [this]() {
        for (auto w : findChildren<QWidget*>()) w->setEnabled(true);
        ui->logToCsvButton->setEnabled(true);
        if (autoScaleYTimer) autoScaleYTimer->start();
        if (plotUpdateTimer) plotUpdateTimer->start();
        ui->statusbar->showMessage("Logging finished.", 3000);
    });
    connect(udpWorker, &UdpWorker::loggingError, this, [this](const QString& msg) {
        QMessageBox::critical(this, "Logging Error", msg);
        for (auto w : findChildren<QWidget*>()) w->setEnabled(true);
        ui->logToCsvButton->setEnabled(true);
    });
}

void MainWindow::on_arrayIndexSpinBox_valueChanged(int value)
{
    QString structText = ui->structTextEdit->toPlainText();
    QList<FieldDef> fields = parseCStruct(structText);
    int structSize = 0;
    auto typeSize = [](const QString &type) -> int {
        if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
        if (type == "int16_t" || type == "uint16_t") return 2;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        return 0;
    };
    for (const FieldDef &field : fields) {
        int sz = typeSize(field.type);
        if (sz == 0) continue;
        structSize += sz * field.count;
    }
    int selectedField = -1;
    int selectedArrayIndex = 0;
    int selectedFieldCount = 1;
    for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
        QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedField = row;
            selectedFieldCount = ui->fieldTableWidget->item(row, 3)->text().toInt();
            break;
        }
    }
    if (selectedField >= 0 && selectedFieldCount > 1) {
        selectedArrayIndex = value;
    }
    bool endianness = ui->endiannessCheckBox->isChecked();
    if (ui->debugLogCheckBox->isChecked()) qDebug() << "[UI] updateUdpConfig (arrayIndex changed):" << structText << structSize << endianness << selectedField << selectedArrayIndex << selectedFieldCount;
    emit updateUdpConfig(structText, fields, structSize, endianness, selectedField, selectedArrayIndex, selectedFieldCount);
    if (selectedField == -1) {
        valueHistory.clear();
        auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
        series->clear();
    }
}

void MainWindow::on_endiannessCheckBox_toggled(bool checked) {
    QString structText = ui->structTextEdit->toPlainText();
    QList<FieldDef> fields = parseCStruct(structText);
    int structSize = 0;
    auto typeSize = [](const QString &type) -> int {
        if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
        if (type == "int16_t" || type == "uint16_t") return 2;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        return 0;
    };
    for (const FieldDef &field : fields) {
        int sz = typeSize(field.type);
        if (sz == 0) continue;
        structSize += sz * field.count;
    }
    int selectedField = -1;
    int selectedArrayIndex = 0;
    int selectedFieldCount = 1;
    for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
        QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selectedField = row;
            selectedFieldCount = ui->fieldTableWidget->item(row, 3)->text().toInt();
            break;
        }
    }
    if (selectedField >= 0 && selectedFieldCount > 1) {
        selectedArrayIndex = ui->arrayIndexSpinBox->value();
    }
    bool endianness = checked;
    if (ui->debugLogCheckBox->isChecked()) qDebug() << "[UI] updateUdpConfig (endianness changed):" << structText << structSize << endianness << selectedField << selectedArrayIndex << selectedFieldCount;
    emit updateUdpConfig(structText, fields, structSize, endianness, selectedField, selectedArrayIndex, selectedFieldCount);
}

bool MainWindow::debugLogEnabled = false;
void MainWindow::setDebugLogEnabled(bool enabled) {
    debugLogEnabled = enabled;
}

