#include "LoggingManager.h"
#include "FieldDef.h"
#include "mainwindow.h"
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QVariant>
#include <vector>
#include "UdpWorker.h"
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
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
    
    if (m_binaryMode) {
        startBinaryLogging();
    } else {
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit loggingError("Failed to open file for writing");
            m_running = false;
            return;
        }
        writeCsvHeader();
    }
    
    m_bytesWritten = 0;
    m_writerThread = std::thread(&LoggingManager::writerThreadFunc, this);
    // Set thread priority for better performance
#ifdef Q_OS_WIN
    // Set thread priority to high on Windows
    SetThreadPriority(reinterpret_cast<HANDLE>(m_writerThread.native_handle()), THREAD_PRIORITY_HIGHEST);
#ifdef ENABLE_DEBUG
    qDebug() << "[LoggingManager] Set logging thread priority to HIGHEST";
#endif
#elif defined(Q_OS_LINUX)
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
    
    if (m_binaryMode) {
        stopBinaryLogging();
    } else {
        flushBuffer();
        m_file.close();
    }
    
    emit loggingFinished();
}

bool LoggingManager::isRunning() const {
    return m_running;
}

bool LoggingManager::enqueuePacket(const QByteArray& packet) { return false; } // No-op now

void LoggingManager::startBinaryLogging() {
    m_binaryMode = true;
    QString binaryFilename = m_filename;
    binaryFilename.replace(".csv", ".bin");
    m_binaryFile.setFileName(binaryFilename);
    
    if (!m_binaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit loggingError("Failed to open binary file for writing");
        return;
    }
    
    // Write binary header
    m_binaryHeader.structSize = m_structSize;
    m_binaryHeader.fieldCount = m_fields.size();
    m_binaryHeader.startTimestamp = QDateTime::currentMSecsSinceEpoch();
    m_binaryFile.write(reinterpret_cast<const char*>(&m_binaryHeader), sizeof(m_binaryHeader));
    m_binaryFile.flush();
    
#ifdef ENABLE_DEBUG
    qDebug() << "[LoggingManager] Started binary logging to" << binaryFilename;
#endif
}

void LoggingManager::stopBinaryLogging() {
    if (m_binaryMode && m_binaryFile.isOpen()) {
        // Update header with final packet count
        m_binaryFile.seek(0);
        m_binaryFile.write(reinterpret_cast<const char*>(&m_binaryHeader), sizeof(m_binaryHeader));
        m_binaryFile.close();
        m_binaryMode = false;
#ifdef ENABLE_DEBUG
        qDebug() << "[LoggingManager] Binary logging stopped. Total packets:" << m_binaryHeader.packetCount;
#endif
    }
}

void LoggingManager::convertBinaryToCSV(const QString& binaryFile, const QString& csvFile) {
    QFile binFile(binaryFile);
    QFile outCsvFile(csvFile);
    
    if (!binFile.open(QIODevice::ReadOnly)) {
#ifdef ENABLE_DEBUG
        qWarning() << "Failed to open binary file:" << binaryFile;
#endif
        return;
    }
    
    if (!outCsvFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
#ifdef ENABLE_DEBUG
        qWarning() << "Failed to open CSV file:" << csvFile;
#endif
        return;
    }
    
    // Read header
    BinaryHeader header;
    if (binFile.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header)) {
#ifdef ENABLE_DEBUG
        qWarning() << "Failed to read binary header";
#endif
        return;
    }
    
    if (header.magic != 0x12345678) {
#ifdef ENABLE_DEBUG
        qWarning() << "Invalid binary file format";
#endif
        return;
    }
    
    // Write CSV header
    QStringList headers;
    for (const FieldDef& f : m_fields) {
        for (int i = 0; i < f.count; ++i) {
            headers << (f.name + (f.count > 1 ? QString("[%1]").arg(i) : ""));
        }
    }
    outCsvFile.write(headers.join(",").toUtf8());
    outCsvFile.write("\n");
    
    // Convert binary data to CSV
    QByteArray buffer;
    const int BATCH_SIZE = 1024 * 1024; // 1MB batches
    buffer.resize(BATCH_SIZE);
    
    qint64 totalBytes = 0;
    while (!binFile.atEnd()) {
        qint64 bytesRead = binFile.read(buffer.data(), BATCH_SIZE);
        if (bytesRead <= 0) break;
        
        // Process each struct in the buffer
        for (qint64 offset = 0; offset <= bytesRead - m_structSize; offset += m_structSize) {
            const char* structPtr = buffer.constData() + offset;
            auto values = extractFieldValues(structPtr, m_structSize, m_fields);
            QStringList row;
            for (const QVariant& v : values) row << v.toString();
            outCsvFile.write(row.join(",").toUtf8());
            outCsvFile.write("\n");
        }
        
        totalBytes += bytesRead;
#ifdef ENABLE_DEBUG
        if (totalBytes % (10 * 1024 * 1024) == 0) { // Every 10MB
            qDebug() << "Converted" << totalBytes / 1024 / 1024 << "MB";
        }
#endif
    }
    
    binFile.close();
    outCsvFile.close();
#ifdef ENABLE_DEBUG
    qDebug() << "Binary to CSV conversion completed:" << binaryFile << "->" << csvFile;
#endif
}

void LoggingManager::writerThreadFunc() {
    if (m_binaryMode) {
        // Binary logging mode - maximum performance
        QByteArray writeBuffer;
        const int flushThreshold = 1024 * 1024; // 1MB for binary logging
        int noDataCount = 0;
        const int MAX_NO_DATA_COUNT = 1000; // 5 seconds at 5ms sleep
        
        while (m_running) {
            UdpWorker::Packet packet;
            bool gotData = false;
            do {
                gotData = m_udpWorker && m_udpWorker->popFromRingBuffer(packet);
                if (gotData) {
                    noDataCount = 0; // Reset counter when we get data
                    // Write packet timestamp and data
                    qint64 timestamp = packet.timestamp;
                    writeBuffer.append(reinterpret_cast<const char*>(&timestamp), sizeof(timestamp));
                    writeBuffer.append(reinterpret_cast<const char*>(&packet.size), sizeof(packet.size));
                    writeBuffer.append(packet.data, packet.size);
                    m_binaryHeader.packetCount++;
                    m_bytesWritten += packet.size + sizeof(timestamp) + sizeof(packet.size);
                }
                if (writeBuffer.size() > flushThreshold) {
                    m_binaryFile.write(writeBuffer);
                    writeBuffer.clear();
                }
            } while (gotData);
            
            if (!gotData) {
                noDataCount++;
#ifdef ENABLE_DEBUG
                if (noDataCount > MAX_NO_DATA_COUNT) {
                    qWarning() << "[LoggingManager] No data received for 5 seconds, logging may be hanging";
                    qWarning() << "[LoggingManager] Check if UDP data is being received and parsed correctly";
                    noDataCount = 0; // Reset to avoid spam
                }
#endif
            }
            
            QThread::usleep(1000); // 1 millisecond for high-rate binary mode
        }
        
        if (!writeBuffer.isEmpty()) {
            m_binaryFile.write(writeBuffer);
            writeBuffer.clear();
        }
        m_binaryFile.flush();
    } else {
        // CSV logging mode (original implementation)
        QByteArray writeBuffer;
        const int flushThreshold = 64 * 1024; // 64KB
        int noDataCount = 0;
        const int MAX_NO_DATA_COUNT = 1000; // 5 seconds at 5ms sleep
        
        while (m_running) {
            UdpWorker::Packet packet;
            bool gotData = false;
            do {
                gotData = m_udpWorker && m_udpWorker->popFromRingBuffer(packet);
                if (gotData) {
                    noDataCount = 0; // Reset counter when we get data
                    int nStructs = packet.size / m_structSize;
#ifdef ENABLE_DEBUG
                    if (nStructs > 0) {
                        qDebug() << "[LoggingManager] Processing" << nStructs << "structs from packet of size" << packet.size;
                    }
#endif
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
            
            if (!gotData) {
                noDataCount++;
#ifdef ENABLE_DEBUG
                if (noDataCount > MAX_NO_DATA_COUNT) {
                    qWarning() << "[LoggingManager] No data received for 5 seconds, logging may be hanging";
                    qWarning() << "[LoggingManager] Check if UDP data is being received and parsed correctly";
                    noDataCount = 0; // Reset to avoid spam
                }
#endif
            }
            
            QThread::usleep(1000); // 1 millisecond for high-rate CSV mode
        }
        if (!writeBuffer.isEmpty()) {
            m_file.write(writeBuffer);
            writeBuffer.clear();
        }
        m_file.flush();
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
    // No-op: all data is drained in writerThreadFunc
    m_file.flush();
}
