#include "mainwindow.h"
#include "ui_mainwindow.h"
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

QT_CHARTS_USE_NAMESPACE

const quint8 FS_COMM_IDF = 0x55;
const quint8 FRQ_COMM_IDF = 0xAA;

// Helper struct for parsed fields
struct FieldDef {
    QString type;
    QString name;
    int count;
};

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , udpSocket(new QUdpSocket(this))
    , updateTimer(new QTimer(this))
    , autoScaleYTimer(new QTimer(this))
{
    ui->setupUi(this);

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
    connect(udpSocket, &QUdpSocket::readyRead,
            this, &MainWindow::readPendingDatagrams);

    // Initialize with default values
    daqPort = ui->portSpinBox->value();
    initializeSocket();

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
    axisY->setRange(0, 30000);

    connect(ui->fieldTableWidget, &QTableWidget::itemChanged,
            this, &MainWindow::on_fieldTableWidget_itemChanged);
    connect(ui->applyFftCheckBox, &QCheckBox::stateChanged, this, &MainWindow::on_applyFftCheckBox_stateChanged);
    connect(ui->fftLengthSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::on_fftLengthSpinBox_valueChanged);

    // Add buffer for time series
    valueHistory.clear();
    maxHistory = 256;

    // Set up sliders
    ui->xDivSlider->setMinimum(10);
    ui->xDivSlider->setMaximum(1000);
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
}

MainWindow::~MainWindow()
{
    delete ui;
    autoScaleYTimer->stop();
}

void MainWindow::initializeSocket()
{
    udpSocket->close();
    if (!udpSocket->bind(QHostAddress::Any, daqPort)) {
        ui->statusbar->showMessage(tr("Failed to bind to port %1").arg(daqPort), 3000);
        qDebug() << "Failed to bind to port" << daqPort;
    } else {
        ui->statusbar->showMessage(tr("Bound to port %1").arg(daqPort), 3000);
        qDebug() << "Bound to port" << daqPort;
    }
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
    initializeSocket();
}

void MainWindow::on_startButton_clicked()
{
    packetLength = ui->packetLengthSpinBox->value();
    QString cmdText = ui->commandLineEdit->text().trimmed();
    QByteArray command;
    if (ui->hexCheckBox->isChecked()) {
        // Remove 0x if present, and spaces
        QString hexStr = cmdText;
        if (hexStr.startsWith("0x") || hexStr.startsWith("0X"))
            hexStr = hexStr.mid(2);
        hexStr = hexStr.remove(' ');
        // Ensure even length
        if (hexStr.length() % 2 != 0) hexStr = "0" + hexStr;
        bool ok = true;
        command = QByteArray::fromHex(hexStr.toUtf8());
        if (command.isEmpty() && !hexStr.isEmpty()) ok = false;
        if (!ok) {
            ui->statusbar->showMessage("Invalid hex command", 3000);
            return;
        }
    } else {
        command = cmdText.toUtf8();
    }
    if (!daqAddress.isNull()) {
        udpSocket->writeDatagram(command, daqAddress, daqPort);
        ui->statusbar->showMessage(tr("Command sent to %1:%2")
            .arg(daqAddress.toString())
            .arg(daqPort), 3000);
    } else {
        ui->statusbar->showMessage("Set DAQ IP first!", 3000);
    }
}

void MainWindow::sendCommand(quint8 commandId, quint32 value)
{
    if (daqAddress.isNull()) {
        QMessageBox::warning(this, "Error", "Set DAQ IP first!");
        return;
    }

    QByteArray command;
    command.append(commandId);
    quint32 leValue = qToLittleEndian(value);
    command.append(reinterpret_cast<const char*>(&leValue), sizeof(leValue));

    udpSocket->writeDatagram(command, daqAddress, daqPort);
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
}
void MainWindow::on_fftLengthSpinBox_valueChanged(int value) {
    fftBuffer.clear();
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
    axisX->setRange(0, points.size() - 1);
    float minVal = *std::min_element(fftResult.begin(), fftResult.end());
    float maxVal = *std::max_element(fftResult.begin(), fftResult.end());
    axisY->setRange(minVal, maxVal * 1.1f);
}

void MainWindow::processFftAndPlot() {
    if ((int)fftBuffer.size() == ui->fftLengthSpinBox->value()) {
        auto fftResult = computeFft(fftBuffer);
        plotFftData(fftResult);
        fftBuffer.clear();
    }
}

void MainWindow::readPendingDatagrams()
{
    static QList<FieldDef> lastParsedFields;
    // Clear cache if struct changed
    static QString lastStructText;
    QString currentStructText = ui->structTextEdit->toPlainText();
    if (currentStructText != lastStructText) {
        lastParsedFields = parseCStruct(currentStructText);
        lastStructText = currentStructText;
    }
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        udpSocket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        if (ui->debugLogCheckBox->isChecked()) qDebug() << "Received datagram of size" << datagram.size();
        if (datagram.size() == packetLength) {
            int selectedField = -1;
            int selectedArrayIndex = 0;
            int selectedFieldCount = 1;
            for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
                QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
                if (!item) {
                    if (ui->debugLogCheckBox->isChecked()) qDebug() << "TableWidget item is null at row" << row;
                    continue;
                }
                if (item->checkState() == Qt::Checked) {
                    selectedField = row;
                    selectedFieldCount = ui->fieldTableWidget->item(row, 3)->text().toInt();
                    break;
                }
            }
            if (selectedField >= 0 && selectedFieldCount > 1) {
                selectedArrayIndex = ui->arrayIndexSpinBox->value();
            }
            bool fftEnabled = ui->applyFftCheckBox->isChecked();
            int fftLen = ui->fftLengthSpinBox->value();
            if (selectedField >= 0) {
                auto typeSize = [](const QString &type) -> int {
                    if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
                    if (type == "int16_t" || type == "uint16_t") return 2;
                    if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
                    if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
                    return 0;
                };
                int offset = 0;
                for (int i = 0; i < selectedField; ++i) {
                    offset += typeSize(ui->fieldTableWidget->item(i, 1)->text()) * ui->fieldTableWidget->item(i, 3)->text().toInt();
                }
                QString type = ui->fieldTableWidget->item(selectedField, 1)->text();
                int typeSz = typeSize(type);
                if (selectedFieldCount > 1) {
                    offset += selectedArrayIndex * typeSz;
                }
                float value = 0.0f;
                if (offset + typeSz <= datagram.size()) {
                    const char* ptr = datagram.constData() + offset;
                    if (type == "int16_t") {
                        value = static_cast<float>(*reinterpret_cast<const int16_t*>(ptr));
                    } else if (type == "uint16_t") {
                        value = static_cast<float>(*reinterpret_cast<const uint16_t*>(ptr));
                    } else if (type == "int32_t") {
                        value = static_cast<float>(*reinterpret_cast<const int32_t*>(ptr));
                    } else if (type == "uint32_t") {
                        value = static_cast<float>(*reinterpret_cast<const uint32_t*>(ptr));
                    } else if (type == "float") {
                        value = *reinterpret_cast<const float*>(ptr);
                    } else if (type == "int64_t") {
                        value = static_cast<float>(*reinterpret_cast<const int64_t*>(ptr));
                    } else if (type == "uint64_t") {
                        value = static_cast<float>(*reinterpret_cast<const uint64_t*>(ptr));
                    } else if (type == "double") {
                        value = static_cast<float>(*reinterpret_cast<const double*>(ptr));
                    } else if (type == "int8_t") {
                        value = static_cast<float>(*reinterpret_cast<const int8_t*>(ptr));
                    } else if (type == "uint8_t" || type == "char") {
                        value = static_cast<float>(*reinterpret_cast<const uint8_t*>(ptr));
                    }
                }
                if (ui->debugLogCheckBox->isChecked()) qDebug() << "Field offset:" << offset << "type:" << type << "value:" << value;
                if (fftEnabled) {
                    fftBuffer.push_back(value);
                    if ((int)fftBuffer.size() >= fftLen) {
                        fftBuffer.resize(fftLen);
                        processFftAndPlot();
                    }
                } else {
                    // Append to time series buffer
                    if (valueHistory.size() >= xDiv) valueHistory.pop_front();
                    valueHistory.append(QPointF(valueHistory.size(), value));
                    if (ui->debugLogCheckBox->isChecked()) qDebug() << "Appending value to history:" << value;
                    // Re-index X for rolling window
                    for (int i = 0; i < valueHistory.size(); ++i) valueHistory[i].setX(i);
                    auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
                    series->replace(valueHistory);
                    if (ui->debugLogCheckBox->isChecked()) qDebug() << "Plotting time series, valueHistory size:" << valueHistory.size();
                    if (ui->debugLogCheckBox->isChecked() && !valueHistory.isEmpty()) {
                        qDebug() << "First point:" << valueHistory.first() << "Last point:" << valueHistory.last();
                    }
                    QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
                    QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());
                    if (axisX) axisX->setRange(0, xDiv - 1);
                    // Y axis: only update here if not auto-scaling
                    if (axisY && !ui->autoScaleYCheckBox->isChecked()) {
                        axisY->setRange(0, yDiv);
                    }
                }
            }
        }
        else if (datagram.size() == 1) {
            quint8 ack = static_cast<quint8>(datagram[0]);
            if (ack == FS_COMM_IDF) {
                ui->statusbar->showMessage("Fs set successfully", 3000);
            }
            else if (ack == FRQ_COMM_IDF) {
                ui->statusbar->showMessage("Frequency set successfully", 3000);
            }
        }
    }
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
    }
}
