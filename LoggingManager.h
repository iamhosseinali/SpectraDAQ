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
#include <QMutex>
#include <QWaitCondition>
#include <boost/lockfree/spsc_queue.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include "FieldDef.h"

class LoggingManager : public QObject {
    Q_OBJECT
public:
    LoggingManager(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename, int bufferCapacity = 1 << 20, QObject* parent = nullptr);
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

    // Lock-free queue for raw packets
    boost::lockfree::spsc_queue<QByteArray*, boost::lockfree::capacity<1 << 20>> m_queue;
    std::vector<QByteArray*> m_packetPool;
    QMutex m_poolMutex;
    QWaitCondition m_poolCond;
};

#endif // LOGGINGMANAGER_H
