#include "LoggingManager.h"
#include "FieldDef.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QVariant>
#include <vector>
#include "UdpWorker.h"
#ifdef Q_OS_LINUX
#include <pthread.h>
#include <sched.h>
#endif

LoggingManager::LoggingManager(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename, UdpWorker* udpWorker, QObject* parent)
    : QObject(parent),
      m_fields(fields),
      m_structSize(structSize),
      m_durationSec(durationSec),
      m_filename(filename),
      m_running(false),
      m_udpWorker(udpWorker),
      m_bytesWritten(0)
{
    m_file.setFileName(m_filename);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &LoggingManager::stop);
}

LoggingManager::~LoggingManager() {
    stop();
}

void LoggingManager::start() {
    if (m_running.exchange(true)) return;
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit loggingError("Failed to open file for writing");
        m_running = false;
        return;
    }
    writeCsvHeader();
    m_bytesWritten = 0;
    m_writerThread = std::thread(&LoggingManager::writerThreadFunc, this);
    // Set thread priority for better performance
#ifdef Q_OS_LINUX
    pthread_t threadHandle = m_writerThread.native_handle();
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(threadHandle, SCHED_FIFO, &sch_params);
#endif
    m_timer->start(m_durationSec * 1000);
}

void LoggingManager::stop() {
    if (!m_running.exchange(false)) return;
    if (m_writerThread.joinable()) m_writerThread.join();
    flushBuffer();
    m_file.close();
    emit loggingFinished();
}

bool LoggingManager::isRunning() const {
    return m_running;
}

bool LoggingManager::enqueuePacket(const QByteArray& packet) { return false; } // No-op now

void LoggingManager::writerThreadFunc() {
    QByteArray writeBuffer;
    const int flushThreshold = 64 * 1024; // 64KB
    while (m_running) {
        UdpWorker::Packet packet;
        bool gotData = false;
        do {
            gotData = m_udpWorker && m_udpWorker->popFromRingBuffer(packet);
            if (gotData) {
                int nStructs = packet.size / m_structSize;
                if (nStructs > 0) {
                    qDebug() << "[LoggingManager] Processing" << nStructs << "structs from packet of size" << packet.size;
                }
                for (int i = 0; i < nStructs; ++i) {
                    const char* structPtr = packet.data + i * m_structSize;
                    auto values = extractFieldValues(structPtr, m_structSize, m_fields);
                    QStringList row;
                    for (const QVariant& v : values) row << v.toString();
                    writeBuffer += row.join(",").toUtf8();
                    writeBuffer += "\n";
                }
                m_bytesWritten += packet.size;
            }
            if (writeBuffer.size() > flushThreshold) {
                m_file.write(writeBuffer);
                writeBuffer.clear();
            }
        } while (gotData);
        QThread::usleep(100);  // Reduced from 1ms to 100 microseconds
    }
    if (!writeBuffer.isEmpty()) {
        m_file.write(writeBuffer);
        writeBuffer.clear();
    }
    m_file.flush();
}

void LoggingManager::writeCsvHeader() {
    QStringList headers;
    for (const FieldDef& f : m_fields) {
        for (int i = 0; i < f.count; ++i) {
            headers << (f.name + (f.count > 1 ? QString("[%1]").arg(i) : ""));
        }
    }
    m_file.write(headers.join(",").toUtf8());
    m_file.write("\n");
}

void LoggingManager::flushBuffer() {
    // No-op: all data is drained in writerThreadFunc
    m_file.flush();
}
