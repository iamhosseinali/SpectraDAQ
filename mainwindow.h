#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUdpSocket>
#include <QTimer>
#include <QByteArray>
#include <QVector>
#include <QHostAddress>
#include <QtCharts>

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

private:
    Ui::MainWindow *ui;
    QUdpSocket *udpSocket;
    QTimer *updateTimer;
    QHostAddress daqAddress;
    quint16 daqPort;
    int packetLength;

    void initializeSocket();
    void parseAndPlotData(const QByteArray &data);
    void sendCommand(quint8 commandId, quint32 value);
};

#endif // MAINWINDOW_H