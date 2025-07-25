#include "LoggingManager.h"
#include "FieldDef.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QVariant>
#include <vector>
#include "UdpWorker.h"

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
    // TODO: If using QThread, set priority to QThread::AboveNormalPriority
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
        QByteArray datagram;
        bool gotData = false;
        do {
            gotData = m_udpWorker && m_udpWorker->popFromRingBuffer(datagram);
            if (gotData) {
                int nStructs = datagram.size() / m_structSize;
                for (int i = 0; i < nStructs; ++i) {
                    const char* structPtr = datagram.constData() + i * m_structSize;
                    QByteArray structView = QByteArray::fromRawData(structPtr, m_structSize);
                    auto values = extractFieldValues(structView, m_fields);
                    QStringList row;
                    for (const QVariant& v : values) row << v.toString();
                    writeBuffer += row.join(",").toUtf8();
                    writeBuffer += "\n";
                }
                m_bytesWritten += datagram.size();
            }
            if (writeBuffer.size() > flushThreshold) {
                m_file.write(writeBuffer);
                writeBuffer.clear();
            }
        } while (gotData);
        QThread::msleep(1);
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
