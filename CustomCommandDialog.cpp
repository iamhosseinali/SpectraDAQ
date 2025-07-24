#include "CustomCommandDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QListWidget>
#include <QInputDialog>
#include <QJsonObject>
#include "CommandEditDialog.h"

// Add Q_OBJECT macro implementation if needed
// Q_OBJECT

CustomCommandDialog::CustomCommandDialog(const QJsonArray &commands, QWidget *parent)
    : QDialog(parent), commandArray(commands)
{
    setWindowTitle("Edit Custom Commands");
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    listWidget = new QListWidget(this);
    mainLayout->addWidget(listWidget);
    QHBoxLayout *btnLayout = new QHBoxLayout;
    QPushButton *addBtn = new QPushButton("Add", this);
    QPushButton *editBtn = new QPushButton("Edit", this);
    QPushButton *removeBtn = new QPushButton("Remove", this);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(editBtn);
    btnLayout->addWidget(removeBtn);
    mainLayout->addLayout(btnLayout);
    // Add Save/Cancel buttons at the bottom
    QHBoxLayout *saveCancelLayout = new QHBoxLayout;
    QPushButton *saveBtn = new QPushButton("Save", this);
    QPushButton *cancelBtn = new QPushButton("Cancel", this);
    saveCancelLayout->addStretch();
    saveCancelLayout->addWidget(saveBtn);
    saveCancelLayout->addWidget(cancelBtn);
    mainLayout->addLayout(saveCancelLayout);
    connect(addBtn, &QPushButton::clicked, this, &CustomCommandDialog::on_addButton_clicked);
    connect(editBtn, &QPushButton::clicked, this, &CustomCommandDialog::on_editButton_clicked);
    connect(removeBtn, &QPushButton::clicked, this, &CustomCommandDialog::on_removeButton_clicked);
    connect(saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    setLayout(mainLayout);
    updateCommandList();
}

QJsonArray CustomCommandDialog::getCommands() const {
    return commandArray;
}

void CustomCommandDialog::updateCommandList() {
    listWidget->clear();
    for (const QJsonValue &val : commandArray) {
        CustomCommandData cmd = CustomCommandData::fromJson(val.toObject());
        QString label = cmd.name + " [" + cmd.type + "]";
        listWidget->addItem(label);
    }
}

void CustomCommandDialog::on_addButton_clicked() {
    CommandEditDialog dlg(this);
    CustomCommandData data;
    data.type = "spinbox";
    data.value_size = 2;
    data.header = "0";
    data.trailer = "0";
    dlg.setCommand(data);
    if (dlg.exec() == QDialog::Accepted) {
        commandArray.append(dlg.getCommand().toJson());
        updateCommandList();
    }
}

void CustomCommandDialog::on_editButton_clicked() {
    int row = listWidget->currentRow();
    if (row < 0 || row >= commandArray.size()) return;
    CustomCommandData data = CustomCommandData::fromJson(commandArray[row].toObject());
    CommandEditDialog dlg(this);
    dlg.setCommand(data);
    if (dlg.exec() == QDialog::Accepted) {
        commandArray[row] = dlg.getCommand().toJson();
        updateCommandList();
    }
}

void CustomCommandDialog::on_removeButton_clicked() {
    int row = listWidget->currentRow();
    if (row < 0 || row >= commandArray.size()) return;
    commandArray.removeAt(row);
    updateCommandList();
} 