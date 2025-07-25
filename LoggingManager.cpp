#include "LoggingManager.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>

LoggingManager::LoggingManager(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename, int bufferCapacity, QObject* parent)
    : QObject(parent),
      m_fields(fields),
      m_structSize(structSize),
      m_durationSec(durationSec),
      m_filename(filename),
      m_running(false),
      m_queue(),
      m_bytesWritten(0)
{
    m_file.setFileName(m_filename);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &LoggingManager::stop);
    // Pre-allocate packet pool
    m_packetPool.reserve(bufferCapacity);
    for (int i = 0; i < bufferCapacity; ++i) {
        m_packetPool.push_back(new QByteArray());
    }
}

LoggingManager::~LoggingManager() {
    stop();
    for (auto ptr : m_packetPool) delete ptr;
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

bool LoggingManager::enqueuePacket(const QByteArray& packet) {
    if (!m_running) return false;
    QByteArray* buf = nullptr;
    {
        QMutexLocker locker(&m_poolMutex);
        if (!m_packetPool.empty()) {
            buf = m_packetPool.back();
            m_packetPool.pop_back();
        }
    }
    if (!buf) return false; // Pool exhausted
    *buf = packet;
    if (!m_queue.push(buf)) {
        // Buffer full, drop packet
        QMutexLocker locker(&m_poolMutex);
        m_packetPool.push_back(buf);
        return false;
    }
    return true;
}

void LoggingManager::writerThreadFunc() {
    std::vector<QByteArray*> batch;
    batch.reserve(1024);
    while (m_running || !m_queue.empty()) {
        batch.clear();
        QByteArray* ptr = nullptr;
        while (m_queue.pop(ptr)) {
            batch.push_back(ptr);
            if (batch.size() >= 1024) break;
        }
        if (!batch.empty()) {
            // Write batch to file
            for (QByteArray* ba : batch) {
                int nStructs = ba->size() / m_structSize;
                for (int i = 0; i < nStructs; ++i) {
                    int offset = i * m_structSize;
                    QString row;
                    for (int j = 0; j < m_structSize; ++j) {
                        if (j > 0) row += ",";
                        row += QString::number(static_cast<unsigned char>((*ba)[offset + j]));
                    }
                    row += "\n";
                    m_file.write(row.toUtf8());
                }
                m_bytesWritten += ba->size();
            }
            m_file.flush();
            QMutexLocker locker(&m_poolMutex);
            for (QByteArray* ba : batch) m_packetPool.push_back(ba);
        } else {
            QThread::msleep(1);
        }
    }
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
    QByteArray* ptr = nullptr;
    while (m_queue.pop(ptr)) {
        int nStructs = ptr->size() / m_structSize;
        for (int i = 0; i < nStructs; ++i) {
            int offset = i * m_structSize;
            QString row;
            for (int j = 0; j < m_structSize; ++j) {
                if (j > 0) row += ",";
                row += QString::number(static_cast<unsigned char>((*ptr)[offset + j]));
            }
            row += "\n";
            m_file.write(row.toUtf8());
        }
        QMutexLocker locker(&m_poolMutex);
        m_packetPool.push_back(ptr);
    }
    m_file.flush();
}
