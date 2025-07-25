#include "mainwindow.h"
#include <QApplication>
#include <QMetaType>
#include "FieldDef.h"
#include <QHostAddress>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<FieldDef>>("QList<FieldDef>");
    qRegisterMetaType<QHostAddress>("QHostAddress");
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
