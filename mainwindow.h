#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QTimer>
#include <QByteArray>
#include <QVector>
#include <QHostAddress>
#include <QtCharts>
#include <vector>
#include <complex>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_startButton_clicked();
    void on_setFsButton_clicked();
    void on_setFreqButton_clicked();
    void readPendingDatagrams();
    void on_ipLineEdit_editingFinished();
    void on_portSpinBox_editingFinished();
    void on_parseStructButton_clicked();
    void on_fieldTableWidget_itemChanged(QTableWidgetItem *item);
    // Optionally, slot for FFT controls
    void on_applyFftCheckBox_stateChanged(int state);
    void on_fftLengthSpinBox_valueChanged(int value);

private:
    Ui::MainWindow *ui;
    QUdpSocket *udpSocket;
    QTimer *updateTimer;
    QHostAddress daqAddress;
    quint16 daqPort;
    int packetLength;

    // FFT related
    std::vector<float> fftBuffer;
    int fftBufferFieldIndex = -1; // Which field is being buffered
    void processFftAndPlot();
    void plotRawData(const QByteArray &data);
    void plotFftData(const std::vector<float> &fftResult);
    std::vector<float> computeFft(const std::vector<float> &data);

    void initializeSocket();
    void parseAndPlotData(const QByteArray &data);
    void sendCommand(quint8 commandId, quint32 value);
};

#endif // MAINWINDOW_H