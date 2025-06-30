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

QT_CHARTS_USE_NAMESPACE

const quint8 FS_COMM_IDF = 0x55;
const quint8 FRQ_COMM_IDF = 0xAA;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , udpSocket(new QUdpSocket(this))
    , updateTimer(new QTimer(this))
{
    ui->setupUi(this);

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
    chart->setTitle("Real-Time FFT Output");
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

    // Default values
    ui->fsSpinBox->setValue(100000);
    ui->freqSpinBox->setValue(1000);
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

void MainWindow::on_setFsButton_clicked()
{
    quint32 fsValue = static_cast<quint32>(ui->fsSpinBox->value());
    sendCommand(FS_COMM_IDF, fsValue);
}

void MainWindow::on_setFreqButton_clicked()
{
    quint32 freqValue = static_cast<quint32>(ui->freqSpinBox->value());
    sendCommand(FRQ_COMM_IDF, freqValue);
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
            parseAndPlotData(datagram);
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
