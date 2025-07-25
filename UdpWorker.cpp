#include "UdpWorker.h"
#include <QDebug>
#include <QtEndian>
#include <algorithm>
#include "mainwindow.h"

UdpWorker::UdpWorker(QObject *parent) : QObject(parent) {}

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
}

void UdpWorker::updateConfig(const QString &structText_, const QList<FieldDef> &fields_, int structSize_, bool endianness_, int selectedField_, int selectedArrayIndex_, int selectedFieldCount_) {
    configure(structText_, fields_, structSize_, endianness_, selectedField_, selectedArrayIndex_, selectedFieldCount_);
}

void UdpWorker::start(quint16 port_) {
    if (udpSocket) return;
    port = port_;
    udpSocket = new QUdpSocket(this);
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
    loggingManager = new LoggingManager(fields, structSize, durationSec, filename);
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

void UdpWorker::processPendingDatagrams() {
    if (!running || !udpSocket) return;
    QVector<float> values;
    while (udpSocket->hasPendingDatagrams() && running) {
        QByteArray datagram;
        datagram.resize(int(udpSocket->pendingDatagramSize()));
        udpSocket->readDatagram(datagram.data(), datagram.size());
        parseDatagram(datagram, values);
        if (loggingManager && loggingManager->isRunning()) {
            loggingManager->enqueuePacket(datagram);
        }
    }
    if (!values.isEmpty()) {
        emit dataReceived(values);
    }
}

void UdpWorker::parseDatagram(const QByteArray &datagram, QVector<float> &values) {
    if (structSize <= 0 || selectedField < 0 || selectedField >= fields.size()) return;
    int numStructs = datagram.size() / structSize;
    const FieldDef &field = fields[selectedField];
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
    int fieldOffset = 0;
    for (int i = 0; i < selectedField; ++i) {
        int sz = typeSize(fields[i].type);
        int align = typeAlignment(fields[i].type);
        int padding = (align - (fieldOffset % align)) % align;
        fieldOffset += padding;
        fieldOffset += sz * fields[i].count;
    }
    int typeSz = typeSize(field.type);
    int fieldAlign = typeAlignment(field.type);
    int fieldPadding = (fieldAlign - (fieldOffset % fieldAlign)) % fieldAlign;
    fieldOffset += fieldPadding;
    if (field.count > 1) {
        fieldOffset += selectedArrayIndex * typeSz;
    }
    for (int structIdx = 0; structIdx < numStructs; ++structIdx) {
        int offset = structIdx * structSize + fieldOffset;
        float value = 0.0f;
        if (offset + typeSz <= datagram.size()) {
            const char* ptr = datagram.constData() + offset;
            bool swap = endianness;
            if (field.type == "int16_t") {
                int16_t v = *reinterpret_cast<const int16_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "uint16_t") {
                uint16_t v = *reinterpret_cast<const uint16_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "int32_t") {
                int32_t v = *reinterpret_cast<const int32_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "uint32_t") {
                uint32_t v = *reinterpret_cast<const uint32_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "float") {
                float v = *reinterpret_cast<const float*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = v;
            } else if (field.type == "int64_t") {
                int64_t v = *reinterpret_cast<const int64_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "uint64_t") {
                uint64_t v = *reinterpret_cast<const uint64_t*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "double") {
                double v = *reinterpret_cast<const double*>(ptr);
                if (swap) v = qFromLittleEndian(v);
                value = static_cast<float>(v);
            } else if (field.type == "int8_t") {
                value = static_cast<float>(*reinterpret_cast<const int8_t*>(ptr));
            } else if (field.type == "uint8_t" || field.type == "char") {
                value = static_cast<float>(*reinterpret_cast<const uint8_t*>(ptr));
            }
        }
        values.append(value);
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