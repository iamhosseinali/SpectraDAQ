#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QVector>
#include "FieldDef.h"
#include "mainwindow.h"
#include <atomic>
#include <vector>
#include <functional>
#include <array>
#include <memory>

class LoggingManager; // Forward declaration

class UdpWorker : public QObject {
    Q_OBJECT
public:
    explicit UdpWorker(QObject *parent = nullptr);
    ~UdpWorker();

    struct Packet {
        char* data = nullptr;
        size_t size = 0;
        qint64 timestamp = 0;
    };

    void configure(const QString &structText, const QList<FieldDef> &fields, int structSize, bool endianness, int selectedField, int selectedArrayIndex, int selectedFieldCount);
    void pushToRingBuffer(const char* data, size_t size);
    bool popFromRingBuffer(Packet& packet);

    using ConverterFunc = std::function<float(const char*, bool)>;

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
    QVector<int> fieldSizes;      // Precomputed sizes for each field
    QVector<int> fieldAlignments; // Precomputed alignments for each field
    int selectedTypeSize = 0;
    int selectedFieldOffset = 0;
    ConverterFunc converter;
    void parseDatagram(const char* data, qint64 size, QVector<float>& values); // Zero-copy version
    void parseDatagram(const QByteArray &datagram, QVector<float> &values); // Old version (optional)
    LoggingManager* loggingManager = nullptr;
    static constexpr int RING_BUFFER_SIZE = 16384;  // Increased from 1024 to 16384
    static constexpr int MAX_PACKET_SIZE = 65536;
    std::array<Packet, RING_BUFFER_SIZE> ringBuffer;
    std::vector<std::unique_ptr<char[]>> packetPool;
    QByteArray recvBuffer;
    std::atomic<int> ringHead{0};
    std::atomic<int> ringTail{0};
}; 