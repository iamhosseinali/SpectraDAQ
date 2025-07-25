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
#include <QJsonObject>
#include <QDialog>
#include "UdpWorker.h"
#include <QThread>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class LoggingManager; // Forward declaration
class UdpWorker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    static bool debugLogEnabled;
    static void setDebugLogEnabled(bool enabled);
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_ipLineEdit_editingFinished();
    void on_portSpinBox_editingFinished();
    void on_parseStructButton_clicked();
    void on_fieldTableWidget_itemChanged(QTableWidgetItem *item);
    // Optionally, slot for FFT controls
    void on_applyFftCheckBox_stateChanged(int state);
    void on_fftLengthSpinBox_editingFinished();
    void updatePlot();
    void on_savePresetButton_clicked();
    void on_loadPresetButton_clicked();
    void on_deletePresetButton_clicked();
    void on_presetComboBox_currentIndexChanged(int index);
    void on_editCommandsButton_clicked();
    void on_logToCsvButton_clicked();
    void handleUdpData(QVector<float> values);
    void on_arrayIndexSpinBox_valueChanged(int value);
    void on_endiannessCheckBox_toggled(bool checked);

signals:
    void startUdp(quint16 port);
    void stopUdp();
    void updateUdpConfig(const QString &structText, const QList<FieldDef> &fields, int structSize, bool endianness, int selectedField, int selectedArrayIndex, int selectedFieldCount);
    void sendCustomDatagram(const QByteArray &data, const QHostAddress &addr, quint16 port);

private:
    Ui::MainWindow *ui;
    QHostAddress daqAddress;
    quint16 daqPort;
    int packetLength;
    QWidget* customCommandsWidget = nullptr;

    // FFT related
    std::vector<float> fftBuffer;
    int fftBufferFieldIndex = -1; // Which field is being buffered
    void processFftAndPlot();
    void plotRawData(const QByteArray &data);
    void plotFftData(const std::vector<float> &fftResult);
    std::vector<float> computeFft(const std::vector<float> &data);

    void parseAndPlotData(const QByteArray &data);
    void sendCommand(quint8 commandId, quint32 value);

    // Time series buffer for plotting
    QVector<QPointF> valueHistory;
    int maxHistory = 256;

    // Add for oscilloscope-style axis scaling
    int xDiv = 256; // default time window (samples)
    int yDiv = 30000; // default y range

    // Timer for throttling auto Y-scaling
    QTimer* autoScaleYTimer;
    QTimer* plotUpdateTimer; // Timer for throttling plot updates

    qint64 sampleIndex = 0; // Track sample index for X axis

    int getStructSize();

    void savePresetToFile(const QString &name);
    void loadPresetFromFile(const QString &name);
    void deletePresetFromFile(const QString &name);
    void updatePresetComboBox();
    QJsonObject collectPreset() const;
    void applyPreset(const QJsonObject &preset);
    void showCustomCommandDialog();
    void updateCustomCommandsUI();
    LoggingManager* loggingManager = nullptr;

    QThread *udpThread = nullptr;
    UdpWorker *udpWorker = nullptr;
};

#endif // MAINWINDOW_H