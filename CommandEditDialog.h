#ifndef COMMANDEDITDIALOG_H
#define COMMANDEDITDIALOG_H

#include <QDialog>
#include "CustomCommandDialog.h"

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox; // Added for QCheckBox

class CommandEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit CommandEditDialog(QWidget *parent = nullptr);
    void setCommand(const CustomCommandData &data);
    CustomCommandData getCommand() const;

private slots:
    void onTypeChanged(const QString &type);

private:
    QLineEdit *nameEdit;
    QComboBox *typeCombo;
    QLineEdit *headerEdit;
    QSpinBox *valueSizeSpin;
    QLineEdit *trailerEdit;
    QLineEdit *commandEdit;
    QCheckBox *swapEndianCheck; // Added for QCheckBox
};

#endif // COMMANDEDITDIALOG_H 