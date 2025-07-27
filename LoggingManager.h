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
};

#endif // LOGGINGMANAGER_H
