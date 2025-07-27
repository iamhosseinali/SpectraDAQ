#include "UdpWorker.h"
#include <QDebug>
#include <QtEndian>
#include <algorithm>
#include "mainwindow.h"
#include "LoggingManager.h"
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#include <winsock2.h>
#elif defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <pthread.h>
#include <sched.h>
#endif

UdpWorker::UdpWorker(QObject *parent) : QObject(parent) {
    packetPool.resize(RING_BUFFER_SIZE);
    for (auto& ptr : packetPool) {
        ptr = std::make_unique<char[]>(MAX_PACKET_SIZE);
    }
    recvBuffer.resize(MAX_PACKET_SIZE);
    ringBuffer.fill(Packet{});
}

UdpWorker::~UdpWorker() {
    stop();
}

void UdpWorker::configure(const QString &structText_, const QList<FieldDef> &fields_, int structSize_, bool endianness_, int selectedField_, int selectedArrayIndex_, int selectedFieldCount_) {
    structText = structText_;
    fields = fields_;
    structSize = structSize_;
    endianness = endianness_;
    selectedField = selectedField_;
    selectedArrayIndex = selectedArrayIndex_;
    selectedFieldCount = selectedFieldCount_;

    // Precompute field offsets, sizes, alignments
    fieldOffsets.clear();
    fieldSizes.clear();
    fieldAlignments.clear();
    int offset = 0;
    auto typeSize = [](const QString &type) -> int {
        if (type == "int8_t" || type == "uint8_t" || type == "char") return 1;
        if (type == "int16_t" || type == "uint16_t") return 2;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        return 0;
    };
    auto typeAlignment = [](const QString &type) -> int {
        if (type == "int64_t" || type == "uint64_t" || type == "double") return 8;
        if (type == "int32_t" || type == "uint32_t" || type == "float") return 4;
        if (type == "int16_t" || type == "uint16_t") return 2;
        return 1;
    };
    for (int i = 0; i < fields.size(); ++i) {
        int align = typeAlignment(fields[i].type);
        int sz = typeSize(fields[i].type);
        int padding = (align - (offset % align)) % align;
        offset += padding;
        fieldOffsets.append(offset);
        fieldSizes.append(sz);
        fieldAlignments.append(align);
        offset += sz * fields[i].count;
    }
    // Precompute selectedTypeSize and selectedFieldOffset
    selectedTypeSize = fieldSizes.value(selectedField, 0);
    selectedFieldOffset = fieldOffsets.value(selectedField, 0);
    if (selectedField >= 0 && selectedField < fields.size() && fields[selectedField].count > 1) {
        selectedFieldOffset += selectedArrayIndex * selectedTypeSize;
    }
    // Precompute converter
    QString type = (selectedField >= 0 && selectedField < fields.size()) ? fields[selectedField].type : "";
    if (type == "int16_t") {
        converter = [](const char* ptr, bool swap) {
            int16_t v = *reinterpret_cast<const int16_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "uint16_t") {
        converter = [](const char* ptr, bool swap) {
            uint16_t v = *reinterpret_cast<const uint16_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "int32_t") {
        converter = [](const char* ptr, bool swap) {
            int32_t v = *reinterpret_cast<const int32_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "uint32_t") {
        converter = [](const char* ptr, bool swap) {
            uint32_t v = *reinterpret_cast<const uint32_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "float") {
        converter = [](const char* ptr, bool swap) {
            float v = *reinterpret_cast<const float*>(ptr);
            if (swap) v = qFromLittleEndian(v);
            return v;
        };
    } else if (type == "int64_t") {
        converter = [](const char* ptr, bool swap) {
            int64_t v = *reinterpret_cast<const int64_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "uint64_t") {
        converter = [](const char* ptr, bool swap) {
            uint64_t v = *reinterpret_cast<const uint64_t*>(ptr);
            return static_cast<float>(swap ? qFromLittleEndian(v) : v);
        };
    } else if (type == "double") {
        converter = [](const char* ptr, bool swap) {
            double v = *reinterpret_cast<const double*>(ptr);
            if (swap) v = qFromLittleEndian(v);
            return static_cast<float>(v);
        };
    } else if (type == "int8_t") {
        converter = [](const char* ptr, bool) {
            return static_cast<float>(*reinterpret_cast<const int8_t*>(ptr));
        };
    } else if (type == "uint8_t" || type == "char") {
        converter = [](const char* ptr, bool) {
            return static_cast<float>(*reinterpret_cast<const uint8_t*>(ptr));
        };
    } else {
        converter = [](const char*, bool) { return 0.0f; };
    }
    // Remove this line, std::array does not support resize
    // ringBuffer.resize(RING_BUFFER_SIZE);
}

void UdpWorker::updateConfig(const QString &structText_, const QList<FieldDef> &fields_, int structSize_, bool endianness_, int selectedField_, int selectedArrayIndex_, int selectedFieldCount_) {
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] updateConfig called with structSize=" << structSize_ << "selectedField=" << selectedField_ << "endianness=" << endianness_;
#endif
    configure(structText_, fields_, structSize_, endianness_, selectedField_, selectedArrayIndex_, selectedFieldCount_);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Configuration updated: structSize=" << structSize << "selectedTypeSize=" << selectedTypeSize;
#endif
}

void UdpWorker::start(quint16 port_) {
    if (udpSocket) return;
    port = port_;
    udpSocket = new QUdpSocket(this);
    // Set Qt buffer size BEFORE binding
    udpSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 64 * 1024 * 1024);  // 64MB
    
    if (!udpSocket->bind(QHostAddress::AnyIPv4, port)) {
        emit errorOccurred(QString("Failed to bind UDP socket on port %1").arg(port));
        return;
    }
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Successfully bound UDP socket to port" << port;
    qDebug() << "[UdpWorker] Socket is ready to receive data on port" << port;
#endif
    
    // Set OS-level buffer size AFTER binding
#ifdef Q_OS_WIN
    int sockfd = udpSocket->socketDescriptor();
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Socket descriptor:" << sockfd;
#endif
    
    if (sockfd == -1) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] ERROR: Invalid socket descriptor!";
        qWarning() << "[UdpWorker] Socket not created properly.";
#endif
        return;
    }
    
    int bufSize = 64 * 1024 * 1024;  // 64MB
    int result = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&bufSize, sizeof(bufSize));
    if (result != 0) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] ERROR: setsockopt failed with error code:" << WSAGetLastError();
        qWarning() << "[UdpWorker] This may be a Windows permission or configuration issue.";
#endif
        
        // Try smaller buffer sizes as fallback
        int fallbackSizes[] = {1024*1024, 256*1024, 64*1024, 32*1024}; // 1MB, 256KB, 64KB, 32KB
        for (int fallbackSize : fallbackSizes) {
            result = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&fallbackSize, sizeof(fallbackSize));
            if (result == 0) {
#ifdef ENABLE_DEBUG
                qDebug() << "[UdpWorker] Successfully set buffer size to" << fallbackSize << "bytes";
#endif
                bufSize = fallbackSize;
                break;
            }
        }
    }
    
    // Verify buffer size was set correctly
    int actualBufSize = 0;
    int optlen = sizeof(actualBufSize);
    result = getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*)&actualBufSize, &optlen);
    if (result != 0) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] ERROR: getsockopt failed with error code:" << WSAGetLastError();
#endif
    }
    
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Requested buffer size:" << bufSize << "bytes, Actual:" << actualBufSize << "bytes";
#endif
    
    if (actualBufSize < bufSize) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] WARNING: Actual buffer size is smaller than requested!";
        qWarning() << "[UdpWorker] This may cause packet drops at high rates.";
        qWarning() << "[UdpWorker] Try running as Administrator for maximum buffer size.";
        qWarning() << "[UdpWorker] Current buffer size should still work for moderate data rates.";
#endif
    }
#elif defined(Q_OS_LINUX)
    int sockfd = udpSocket->socketDescriptor();
    int bufSize = 64 * 1024 * 1024;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
    
    // Verify buffer size was set correctly
    int actualBufSize = 0;
    socklen_t optlen = sizeof(actualBufSize);
    getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &actualBufSize, &optlen);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Requested buffer size:" << bufSize << "bytes, Actual:" << actualBufSize << "bytes";
#endif
    
    if (actualBufSize < bufSize) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] WARNING: Actual buffer size is smaller than requested!";
        qWarning() << "[UdpWorker] This may cause packet drops at high rates.";
        qWarning() << "[UdpWorker] Try running with sudo or increase system limits.";
#endif
    }
#endif
    
    connect(udpSocket, &QUdpSocket::readyRead, this, &UdpWorker::processPendingDatagrams);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Connected readyRead signal to processPendingDatagrams";
#endif
    running = true;
    
    // Set UDP thread priority for better performance
#ifdef Q_OS_WIN
    // Set thread priority to high on Windows
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Set UDP thread priority to HIGHEST";
#endif
#elif defined(Q_OS_LINUX)
    pthread_t threadHandle = QThread::currentThread()->native_handle();
    sched_param sch_params;
    sch_params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(threadHandle, SCHED_FIFO, &sch_params);
#endif
    
    // Add a timer to check if we're receiving data
    QTimer* dataCheckTimer = new QTimer(this);
    connect(dataCheckTimer, &QTimer::timeout, this, [this]() {
        static int noDataCount = 0;
        if (running && udpSocket) {
            if (udpSocket->hasPendingDatagrams()) {
                noDataCount = 0;
            } else {
                noDataCount++;
#ifdef ENABLE_DEBUG
                if (noDataCount == 10) { // 10 seconds
                    qWarning() << "[UdpWorker] No UDP data received for 10 seconds!";
                    qWarning() << "[UdpWorker] Check: 1) Data source is sending to port" << port;
                    qWarning() << "[UdpWorker] Check: 2) Windows Firewall is not blocking UDP";
                    qWarning() << "[UdpWorker] Check: 3) Network interface is correct";
                }
#endif
            }
        }
    });
    dataCheckTimer->start(1000); // Check every second
}

void UdpWorker::stop() {
    running = false;
    if (udpSocket) {
        udpSocket->close();
        udpSocket->deleteLater();
        udpSocket = nullptr;
    }
}

void UdpWorker::setRunning(bool run) {
    running = run;
}

void UdpWorker::startLogging(const QList<FieldDef>& fields, int structSize, int durationSec, const QString& filename) {
    if (loggingManager) {
        delete loggingManager;
        loggingManager = nullptr;
    }
    loggingManager = new LoggingManager(fields, structSize, durationSec, filename, this);
    
    // Enable binary mode if it was previously enabled
    if (binaryLoggingEnabled) {
        loggingManager->enableBinaryMode(true);
    }
    
    connect(loggingManager, &LoggingManager::loggingFinished, this, &UdpWorker::loggingFinished);
    connect(loggingManager, &LoggingManager::loggingError, this, &UdpWorker::loggingError);
    connect(loggingManager, &LoggingManager::conversionFinished, this, &UdpWorker::conversionFinished);
    loggingManager->start();
}

void UdpWorker::stopLogging() {
    if (loggingManager) {
        loggingManager->stop();
        delete loggingManager;
        loggingManager = nullptr;
    }
}

void UdpWorker::enableBinaryLogging(bool enable) {
    binaryLoggingEnabled = enable;  // Store the state
    if (loggingManager) {
        loggingManager->enableBinaryMode(enable);
#ifdef ENABLE_DEBUG
        qDebug() << "[UdpWorker] Binary logging mode" << (enable ? "enabled" : "disabled");
#endif
    }
}

void UdpWorker::convertBinaryToCSV(const QString& binaryFile, const QString& csvFile) {
    if (loggingManager) {
        loggingManager->convertBinaryToCSV(binaryFile, csvFile);
#ifdef ENABLE_DEBUG
        qDebug() << "[UdpWorker] Converting binary file to CSV:" << binaryFile << "->" << csvFile;
#endif
    } else {
        qWarning() << "[UdpWorker] No logging manager available for conversion";
    }
}

void UdpWorker::pushToRingBuffer(const char* data, size_t size) {
    int currentHead = ringHead.load(std::memory_order_relaxed);
    int nextHead = (currentHead + 1) % RING_BUFFER_SIZE;
    
    // For high-rate data, drop packets if buffer is full instead of blocking
    if (nextHead == ringTail.load(std::memory_order_acquire)) {
        // Buffer is full, drop this packet for high-rate scenarios
        static int dropCount = 0;
        dropCount++;
#ifdef ENABLE_DEBUG
        if (dropCount % 1000 == 0) {
            qWarning() << "[UdpWorker] Dropped" << dropCount << "packets due to full ring buffer";
        }
#endif
        return;
    }
    
    char* buffer = packetPool[currentHead].get();
    memcpy(buffer, data, size);
    ringBuffer[currentHead] = {buffer, size, QDateTime::currentMSecsSinceEpoch()};
    ringHead.store(nextHead, std::memory_order_release);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Pushed packet of size" << size << "to ring buffer at position" << currentHead;
#endif
}

bool UdpWorker::popFromRingBuffer(Packet& packet) {
    int currentTail = ringTail.load(std::memory_order_relaxed);
    if (currentTail == ringHead.load(std::memory_order_acquire)) {
        return false; // Empty
    }
    packet = ringBuffer[currentTail];
    ringTail.store((currentTail + 1) % RING_BUFFER_SIZE, std::memory_order_release);
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] Popped packet of size" << packet.size << "from ring buffer at position" << currentTail;
#endif
    return true;
}

void UdpWorker::processPendingDatagrams() {
    if (!running || !udpSocket) return;
    
#ifdef ENABLE_DEBUG
    static int callCount = 0;
    callCount++;
    if (callCount % 100 == 0) { // Log every 100 calls
        qDebug() << "[UdpWorker] processPendingDatagrams called" << callCount << "times";
    }
#endif
    
    QVector<float> allValues;
    const int MAX_BATCH = 1000;  // Reduced for more frequent updates
    int processed = 0;
    
    // Process all available datagrams with reasonable limits
    while (udpSocket->hasPendingDatagrams() && running && processed < MAX_BATCH) {
        qint64 size = udpSocket->pendingDatagramSize();
        if (size > MAX_PACKET_SIZE) {
            udpSocket->readDatagram(recvBuffer.data(), MAX_PACKET_SIZE); // Skip oversized packet
            continue;
        }
        qint64 read = udpSocket->readDatagram(recvBuffer.data(), size);
        if (read != size) continue;
        
#ifdef ENABLE_DEBUG
        static int totalReceived = 0;
        totalReceived++;
        if (totalReceived % 100 == 0) { // Log every 100 packets instead of 10
            qDebug() << "[UdpWorker] Received datagram #" << totalReceived << "of size:" << size << "bytes";
        }
#endif
        
        parseDatagram(recvBuffer.constData(), size, allValues);
        pushToRingBuffer(recvBuffer.constData(), size);
        processed++;
    }
    
#ifdef ENABLE_DEBUG
    if (processed > 0) {
        static int totalProcessed = 0;
        totalProcessed += processed;
        if (totalProcessed % 1000 == 0) { // Log every 1000 packets
            qDebug() << "[UdpWorker] Total packets processed:" << totalProcessed << "with" << allValues.size() << "values";
        }
    }
#endif
    
    if (!allValues.isEmpty()) {
#ifdef ENABLE_DEBUG
        static int signalCount = 0;
        signalCount++;
        if (signalCount % 100 == 0) { // Log every 100 signals
            qDebug() << "[UdpWorker] Emitting dataReceived signal #" << signalCount << "with" << allValues.size() << "values";
        }
#endif
        emit dataReceived(allValues);
    } else if (processed > 0) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] WARNING: Processed" << processed << "datagrams but extracted 0 values!";
#endif
    }
    
#ifdef ENABLE_DEBUG
    // Check ring buffer status
    int ringBufferSize = (ringHead.load() - ringTail.load() + RING_BUFFER_SIZE) % RING_BUFFER_SIZE;
    if (ringBufferSize > 0) {
        qDebug() << "[UdpWorker] Ring buffer has" << ringBufferSize << "packets waiting";
    }
#endif
}

void UdpWorker::parseDatagram(const char* data, qint64 size, QVector<float>& values) {
    if (structSize <= 0 || selectedTypeSize == 0) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] parseDatagram: structSize=" << structSize << "selectedTypeSize=" << selectedTypeSize;
#endif
        return;
    }
    int numStructs = size / structSize;
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] parseDatagram: size=" << size << "structSize=" << structSize << "numStructs=" << numStructs;
#endif
    
#ifdef ENABLE_DEBUG
    // Always log the first few packets to see what's happening
    static int packetCount = 0;
    packetCount++;
    if (packetCount <= 5) {
        qDebug() << "[UdpWorker] Packet" << packetCount << ": size=" << size << "structSize=" << structSize << "numStructs=" << numStructs;
        qDebug() << "[UdpWorker] selectedFieldOffset=" << selectedFieldOffset << "selectedTypeSize=" << selectedTypeSize;
    }
#endif
    
    for (int structIdx = 0; structIdx < numStructs; ++structIdx) {
        int offset = structIdx * structSize + selectedFieldOffset;
        if (offset + selectedTypeSize > size) break;
        float value = converter(data + offset, endianness);
        values.append(value);
#ifdef ENABLE_DEBUG
        if (structIdx < 3) {
            qDebug() << "[UdpWorker] Struct" << structIdx << "offset" << offset << "value:" << value;
        }
        // Always log the first few values
        if (packetCount <= 5 && structIdx < 3) {
            qDebug() << "[UdpWorker] Extracted value:" << value << "at offset" << offset;
        }
#endif
    }
    if (values.isEmpty() && numStructs > 0) {
#ifdef ENABLE_DEBUG
        qWarning() << "[UdpWorker] WARNING: No values extracted from" << numStructs << "structs!";
        qWarning() << "[UdpWorker] Check structSize=" << structSize << "selectedFieldOffset=" << selectedFieldOffset << "selectedTypeSize=" << selectedTypeSize;
#endif
    }
}

void UdpWorker::onSocketError(QAbstractSocket::SocketError socketError) {
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] udpSocket error:" << socketError;
#endif
}

void UdpWorker::sendDatagram(const QByteArray &data, const QHostAddress &addr, quint16 port) {
#ifdef ENABLE_DEBUG
    qDebug() << "[UdpWorker] sendDatagram called" << data.toHex() << addr << port;
#endif
    if (udpSocket) {
        QObject::connect(udpSocket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onSocketError(QAbstractSocket::SocketError)));
        udpSocket->writeDatagram(data, addr, port);
    } else {
        QUdpSocket tempSocket;
        tempSocket.writeDatagram(data, addr, port);
    }
} 