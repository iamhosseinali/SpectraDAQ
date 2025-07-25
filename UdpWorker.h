#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QVector>
#include "FieldDef.h"
#include "mainwindow.h"
#include "LoggingManager.h"

class UdpWorker : public QObject {
    Q_OBJECT
public:
    explicit UdpWorker(QObject *parent = nullptr);
    ~UdpWorker();

    void configure(const QString &structText, const QList<FieldDef> &fields, int structSize, bool endianness, int selectedField, int selectedArrayIndex, int selectedFieldCount);

public slots:
    void start(quint16 port);
    void stop();
    void setRunning(bool run);
    void updateConfig(const QString &structText, const QList<FieldDef> &fields, int structSize, bool endianness, int selectedField, int selectedArrayIndex, int selectedFieldCount);
    void sendDatagram(const QByteArray &data, const QHostAddress &addr, quint16 port);
    void startLogging(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename);
    void stopLogging();

private slots:
    void processPendingDatagrams();
    void onSocketError(QAbstractSocket::SocketError socketError);

signals:
    void dataReceived(QVector<float> values); // Send parsed values to UI
    void ackReceived(quint8 ack);
    void errorOccurred(const QString &msg);
    void loggingFinished();
    void loggingError(const QString& msg);

private:
    QUdpSocket *udpSocket = nullptr;
    bool running = false;
    quint16 port = 0;
    QString structText;
    QList<FieldDef> fields;
    int structSize = 0;
    bool endianness = false;
    int selectedField = -1;
    int selectedArrayIndex = 0;
    int selectedFieldCount = 1;
    QVector<int> fieldOffsets; // Precomputed offsets for each field
    void parseDatagram(const QByteArray &datagram, QVector<float> &values);
    LoggingManager* loggingManager = nullptr;
}; 