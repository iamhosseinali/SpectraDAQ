#ifndef CUSTOMCOMMANDDIALOG_H
#define CUSTOMCOMMANDDIALOG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QListWidget>

struct CustomCommandData {
    QString name;
    QString type; // "spinbox" or "button"
    QString header; // hex string, or "0"
    int value_size; // 0, 1, 2, or 4 (bytes)
    QString trailer; // hex string, or "0"
    QString command; // for button type
    bool swap_endian = false; // new: swap endianness for value only
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["name"] = name;
        obj["type"] = type;
        obj["header"] = header;
        obj["value_size"] = value_size;
        obj["trailer"] = trailer;
        obj["command"] = command;
        obj["swap_endian"] = swap_endian;
        return obj;
    }
    static CustomCommandData fromJson(const QJsonObject &obj) {
        CustomCommandData d;
        d.name = obj["name"].toString();
        d.type = obj["type"].toString();
        d.header = obj["header"].toString();
        d.value_size = obj["value_size"].toInt();
        d.trailer = obj["trailer"].toString();
        d.command = obj["command"].toString();
        d.swap_endian = obj.contains("swap_endian") ? obj["swap_endian"].toBool() : false;
        return d;
    }
};

class CustomCommandDialog : public QDialog {
    Q_OBJECT
public:
    explicit CustomCommandDialog(const QJsonArray &commands, QWidget *parent = nullptr);
    QJsonArray getCommands() const;

private slots:
    void on_addButton_clicked();
    void on_editButton_clicked();
    void on_removeButton_clicked();

private:
    void updateCommandList();
    QJsonArray commandArray;
    QListWidget *listWidget;
};

#endif // CUSTOMCOMMANDDIALOG_H 