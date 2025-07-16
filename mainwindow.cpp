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
{
    ui->setupUi(this);

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
}

MainWindow::~MainWindow()
{
    delete ui;
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
    QByteArray startCommand = "StartLog";

    if (!daqAddress.isNull()) {
        udpSocket->writeDatagram(startCommand, daqAddress, daqPort);
        ui->statusbar->showMessage(tr("StartLog sent to %1:%2")
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

void MainWindow::plotRawData(const QByteArray &data) {
    // fallback to original parseAndPlotData
    parseAndPlotData(data);
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
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;

        udpSocket->readDatagram(datagram.data(), datagram.size(),
                               &sender, &senderPort);

        if (datagram.size() == packetLength) {
            // Find selected field index
            int selectedField = -1;
            for (int row = 0; row < ui->fieldTableWidget->rowCount(); ++row) {
                QTableWidgetItem *item = ui->fieldTableWidget->item(row, 0);
                if (item && item->checkState() == Qt::Checked) {
                    selectedField = row;
                    break;
                }
            }
            bool fftEnabled = ui->applyFftCheckBox->isChecked();
            int fftLen = ui->fftLengthSpinBox->value();
            if (fftEnabled && selectedField >= 0) {
                // Assume each field is int32_t, contiguous, as in parseAndPlotData
                const int32_t* samples = reinterpret_cast<const int32_t*>(datagram.constData());
                int sampleCount = datagram.size() / sizeof(int32_t);
                // If struct has multiple fields, select the right one
                // For now, assume each field is 1 int32_t (can be extended for arrays)
                if (selectedField < sampleCount) {
                    float value = static_cast<float>(samples[selectedField]) / 65536.0f;
                    fftBuffer.push_back(value);
                    if ((int)fftBuffer.size() >= fftLen) {
                        fftBuffer.resize(fftLen);
                        processFftAndPlot();
                    }
                }
            } else {
                plotRawData(datagram);
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

void MainWindow::parseAndPlotData(const QByteArray &data)
{
    QVector<QPointF> points;
    const int32_t* samples = reinterpret_cast<const int32_t*>(data.constData());
    int sampleCount = data.size() / sizeof(int32_t);

    float minVal = std::numeric_limits<float>::max();
    float maxVal = std::numeric_limits<float>::lowest();
    bool significantValuesFound = false;

    for (int i = 0; i < sampleCount; ++i) {
        float value = static_cast<float>(samples[i]) / 65536.0f;

        if(i > 0) {
            if(value < minVal) minVal = value;
            if(value > maxVal) maxVal = value;
            if(value > 0.1f) significantValuesFound = true;
        }

        points.append(QPointF(i, value));
    }

    if(!significantValuesFound) {
        minVal = 0;
        maxVal = 1.0f;
    } else {
        float range = maxVal - minVal;
        minVal = qMax(0.0f, minVal - range * 0.1f);
        maxVal *= 1.1f;
    }

    auto *series = static_cast<QLineSeries*>(ui->chartView->chart()->series().at(0));
    series->replace(points);

    QValueAxis* axisX = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Horizontal).first());
    QValueAxis* axisY = qobject_cast<QValueAxis*>(ui->chartView->chart()->axes(Qt::Vertical).first());

    axisX->setRange(0, sampleCount - 1);
    axisY->setRange(minVal, maxVal);
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
    }
}
