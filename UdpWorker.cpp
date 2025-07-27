#include "UdpWorker.h"
#include <QDebug>
#include <QtEndian>
#include <algorithm>
#include "mainwindow.h"
#include "LoggingManager.h"
#include <QDateTime>
#ifdef Q_OS_LINUX
#include <sys/socket.h>
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
    configure(structText_, fields_, structSize_, endianness_, selectedField_, selectedArrayIndex_, selectedFieldCount_);
}

void UdpWorker::start(quint16 port_) {
    if (udpSocket) return;
    port = port_;
    udpSocket = new QUdpSocket(this);
    // Set Qt buffer size BEFORE binding
    udpSocket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 64 * 1024 * 1024);  // 64MB
    // Set OS-level buffer size (Linux)
#ifdef Q_OS_LINUX
    int sockfd = udpSocket->socketDescriptor();
    int bufSize = 64 * 1024 * 1024;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize));
#endif
    if (!udpSocket->bind(QHostAddress::AnyIPv4, port)) {
        emit errorOccurred(QString("Failed to bind UDP socket on port %1").arg(port));
        return;
    }
    connect(udpSocket, &QUdpSocket::readyRead, this, &UdpWorker::processPendingDatagrams);
    running = true;
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
    connect(loggingManager, &LoggingManager::loggingFinished, this, &UdpWorker::loggingFinished);
    connect(loggingManager, &LoggingManager::loggingError, this, &UdpWorker::loggingError);
    loggingManager->start();
}

void UdpWorker::stopLogging() {
    if (loggingManager) {
        loggingManager->stop();
        delete loggingManager;
        loggingManager = nullptr;
    }
}

void UdpWorker::pushToRingBuffer(const char* data, size_t size) {
    int currentHead = ringHead.load(std::memory_order_relaxed);
    int nextHead = (currentHead + 1) % RING_BUFFER_SIZE;
    
    // Wait if buffer is full instead of dropping packets
    while (nextHead == ringTail.load(std::memory_order_acquire)) {
        // Buffer is full, wait a bit for logging thread to catch up
        QThread::usleep(100);  // 100 microseconds
        currentHead = ringHead.load(std::memory_order_relaxed);
        nextHead = (currentHead + 1) % RING_BUFFER_SIZE;
    }
    
    char* buffer = packetPool[currentHead].get();
    memcpy(buffer, data, size);
    ringBuffer[currentHead] = {buffer, size, QDateTime::currentMSecsSinceEpoch()};
    ringHead.store(nextHead, std::memory_order_release);
    if (MainWindow::debugLogEnabled) {
        qDebug() << "[UdpWorker] Pushed packet of size" << size << "to ring buffer at position" << currentHead;
    }
}

bool UdpWorker::popFromRingBuffer(Packet& packet) {
    int currentTail = ringTail.load(std::memory_order_relaxed);
    if (currentTail == ringHead.load(std::memory_order_acquire)) {
        return false; // Empty
    }
    packet = ringBuffer[currentTail];
    ringTail.store((currentTail + 1) % RING_BUFFER_SIZE, std::memory_order_release);
    if (MainWindow::debugLogEnabled) {
        qDebug() << "[UdpWorker] Popped packet of size" << packet.size << "from ring buffer at position" << currentTail;
    }
    return true;
}

void UdpWorker::processPendingDatagrams() {
    if (!running || !udpSocket) return;
    QVector<float> allValues;
    const int MAX_BATCH = 8192;  // Increased to 8192 for high-rate data
    int processed = 0;
    
    // Process all available datagrams without artificial limits
    while (udpSocket->hasPendingDatagrams() && running) {
        qint64 size = udpSocket->pendingDatagramSize();
        if (size > MAX_PACKET_SIZE) {
            udpSocket->readDatagram(recvBuffer.data(), MAX_PACKET_SIZE); // Skip oversized packet
            continue;
        }
        qint64 read = udpSocket->readDatagram(recvBuffer.data(), size);
        if (read != size) continue;
        parseDatagram(recvBuffer.constData(), size, allValues);
        pushToRingBuffer(recvBuffer.constData(), size);
        processed++;
        
        // Only limit if we're processing too many at once to prevent blocking
        if (processed > MAX_BATCH) {
            break;
        }
    }
    if (!allValues.isEmpty()) emit dataReceived(allValues);
    
    // Diagnostic output for high-rate monitoring
    if (MainWindow::debugLogEnabled && processed > 0) {
        static int totalProcessed = 0;
        totalProcessed += processed;
        if (totalProcessed % 10000 == 0) {
            qDebug() << "[UdpWorker] Total packets processed:" << totalProcessed;
        }
    }
}

void UdpWorker::parseDatagram(const char* data, qint64 size, QVector<float>& values) {
    if (structSize <= 0 || selectedTypeSize == 0) return;
    int numStructs = size / structSize;
    for (int structIdx = 0; structIdx < numStructs; ++structIdx) {
        int offset = structIdx * structSize + selectedFieldOffset;
        if (offset + selectedTypeSize > size) break;
        values.append(converter(data + offset, endianness));
    }
}

void UdpWorker::onSocketError(QAbstractSocket::SocketError socketError) {
    if (MainWindow::debugLogEnabled) qDebug() << "[UdpWorker] udpSocket error:" << socketError;
}

void UdpWorker::sendDatagram(const QByteArray &data, const QHostAddress &addr, quint16 port) {
    if (MainWindow::debugLogEnabled) qDebug() << "[UdpWorker] sendDatagram called" << data.toHex() << addr << port;
    if (udpSocket) {
        QObject::connect(udpSocket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onSocketError(QAbstractSocket::SocketError)));
        udpSocket->writeDatagram(data, addr, port);
    } else {
        QUdpSocket tempSocket;
        tempSocket.writeDatagram(data, addr, port);
    }
} 