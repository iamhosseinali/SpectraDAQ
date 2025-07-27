#ifndef LOGGINGMANAGER_H
#define LOGGINGMANAGER_H

#include <QObject>
#include <QFile>
#include <QThread>
#include <QTimer>
#include <QAtomicInt>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <atomic>
#include <thread>
#include <vector>
#include "FieldDef.h"

class UdpWorker; // Forward declaration

class LoggingManager : public QObject {
    Q_OBJECT
public:
    LoggingManager(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename, UdpWorker* udpWorker, QObject* parent = nullptr);
    ~LoggingManager();

    // Called from UDP receive thread
    bool enqueuePacket(const QByteArray& packet);

    // Start/stop logging
    void start();
    void stop();
    bool isRunning() const;
    
    // Binary logging methods
    void enableBinaryMode(bool enable = true) { m_binaryMode = enable; }
    void startBinaryLogging();
    void stopBinaryLogging();
    void convertBinaryToCSV(const QString& binaryFile, const QString& csvFile);

signals:
    void loggingFinished();
    void loggingError(const QString& msg);
    void loggingProgress(qint64 bytesWritten);

private:
    void writerThreadFunc();
    void writeCsvHeader();
    void flushBuffer();

    QList<FieldDef> m_fields;
    int m_structSize;
    int m_durationSec;
    QString m_filename;
    QFile m_file;
    std::atomic<bool> m_running;
    std::thread m_writerThread;
    QAtomicInt m_bytesWritten;
    QTimer* m_timer;

    UdpWorker* m_udpWorker = nullptr;
    
    // Binary logging members
    QFile m_binaryFile;
    bool m_binaryMode = false;
    struct BinaryHeader {
        uint32_t magic = 0x12345678;
        uint32_t version = 1;
        uint32_t structSize;
        uint32_t fieldCount;
        uint64_t startTimestamp;
        uint64_t packetCount = 0;
    } m_binaryHeader;
};

#endif // LOGGINGMANAGER_H
