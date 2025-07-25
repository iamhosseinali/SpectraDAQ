#include "FieldDef.h"
#include <QtEndian>
#include <cstring>
#include <cstdint>

// Helper to swap endianness for various types
template<typename T>
T swapEndianHelper(T u) {
    static_assert (CHAR_BIT == 8, "CHAR_BIT != 8");
    union {
        T u;
        unsigned char u8[sizeof(T)];
    } source, dest;
    source.u = u;
    for (size_t k = 0; k < sizeof(T); k++)
        dest.u8[k] = source.u8[sizeof(T) - k - 1];
    return dest.u;
}

std::vector<QVariant> extractFieldValues(const QByteArray& structData, const QList<FieldDef>& fields, bool swapEndian) {
    std::vector<QVariant> result;
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
    for (const FieldDef& field : fields) {
        int sz = typeSize(field.type);
        int align = typeAlignment(field.type);
        int padding = (align - (offset % align)) % align;
        offset += padding;
        for (int i = 0; i < field.count; ++i) {
            if (offset + sz > structData.size()) {
                result.push_back(QVariant());
                offset += sz;
                continue;
            }
            const char* ptr = structData.constData() + offset;
            if (field.type == "int8_t") {
                int8_t v = *reinterpret_cast<const int8_t*>(ptr);
                result.push_back(v);
            } else if (field.type == "uint8_t" || field.type == "char") {
                uint8_t v = *reinterpret_cast<const uint8_t*>(ptr);
                result.push_back(v);
            } else if (field.type == "int16_t") {
                int16_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "uint16_t") {
                uint16_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "int32_t") {
                int32_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "uint32_t") {
                uint32_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "float") {
                float v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "int64_t") {
                int64_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "uint64_t") {
                uint64_t v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else if (field.type == "double") {
                double v;
                std::memcpy(&v, ptr, sizeof(v));
                if (swapEndian) v = swapEndianHelper(v);
                result.push_back(v);
            } else {
                result.push_back(QVariant());
            }
            offset += sz;
        }
    }
    return result;
} 