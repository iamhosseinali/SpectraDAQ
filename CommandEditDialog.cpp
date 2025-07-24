#include "CommandEditDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>

CommandEditDialog::CommandEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Edit Command");
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;
    nameEdit = new QLineEdit(this);
    typeCombo = new QComboBox(this);
    typeCombo->addItem("spinbox");
    typeCombo->addItem("button");
    headerEdit = new QLineEdit(this);
    valueSizeSpin = new QSpinBox(this);
    valueSizeSpin->setRange(0, 4);
    valueSizeSpin->setSingleStep(1);
    valueSizeSpin->setSuffix(" bytes");
    trailerEdit = new QLineEdit(this);
    commandEdit = new QLineEdit(this);
    form->addRow("Name", nameEdit);
    form->addRow("Type", typeCombo);
    form->addRow("Header (hex)", headerEdit);
    form->addRow("Value Size", valueSizeSpin);
    form->addRow("Trailer (hex)", trailerEdit);
    form->addRow("Command (string/hex)", commandEdit);
    mainLayout->addLayout(form);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(this);
    QPushButton *saveButton = new QPushButton("Save", this);
    QPushButton *cancelButton = new QPushButton("Cancel", this);
    buttonBox->addButton(saveButton, QDialogButtonBox::AcceptRole);
    buttonBox->addButton(cancelButton, QDialogButtonBox::RejectRole);
    mainLayout->addWidget(buttonBox);
    connect(saveButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(typeCombo, &QComboBox::currentTextChanged, this, &CommandEditDialog::onTypeChanged);
    onTypeChanged(typeCombo->currentText());
}

void CommandEditDialog::onTypeChanged(const QString &type) {
    bool isSpinbox = (type == "spinbox");
    headerEdit->setVisible(isSpinbox);
    valueSizeSpin->setVisible(isSpinbox);
    trailerEdit->setVisible(isSpinbox);
    commandEdit->setVisible(!isSpinbox);
}

void CommandEditDialog::setCommand(const CustomCommandData &data) {
    nameEdit->setText(data.name);
    int idx = typeCombo->findText(data.type);
    if (idx >= 0) typeCombo->setCurrentIndex(idx);
    headerEdit->setText(data.header);
    valueSizeSpin->setValue(data.value_size);
    trailerEdit->setText(data.trailer);
    commandEdit->setText(data.command);
    onTypeChanged(data.type);
}

CustomCommandData CommandEditDialog::getCommand() const {
    CustomCommandData data;
    data.name = nameEdit->text();
    data.type = typeCombo->currentText();
    data.header = headerEdit->text();
    data.value_size = valueSizeSpin->value();
    data.trailer = trailerEdit->text();
    data.command = commandEdit->text();
    return data;
} 