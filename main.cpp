#include "mainwindow.h"
#include <QApplication>
#include <QMetaType>
#include "FieldDef.h"
#include <QHostAddress>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
#ifdef ENABLE_DEBUG
    // Allocate console for Windows to show debug output
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    printf("SpectraDAQ Console Output\n");
    printf("========================\n\n");
#endif
#endif

    qRegisterMetaType<QList<FieldDef>>("QList<FieldDef>");
    qRegisterMetaType<QHostAddress>("QHostAddress");
    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
