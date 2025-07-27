#ifndef FIELDDEF_H
#define FIELDDEF_H

#include <QString>
#include <QList>
#include <QVariant>
#include <QByteArray>
#include <vector>

struct FieldDef {
    QString type;
    QString name;
    int count;
};

// Extracts all field values from a struct buffer, returns as QVariant (int, double, etc.)
std::vector<QVariant> extractFieldValues(const QByteArray& structData, const QList<FieldDef>& fields, bool swapEndian = false);

// Zero-copy version that works with raw pointers
std::vector<QVariant> extractFieldValues(const char* data, size_t size, const QList<FieldDef>& fields, bool swapEndian = false);

#endif // FIELDDEF_H 