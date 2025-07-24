QT       += core gui widgets charts network

TARGET = SpectraDAQ
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17

SOURCES += \
        main.cpp \
        mainwindow.cpp \
        CustomCommandDialog.cpp \
        CommandEditDialog.cpp

HEADERS += \
        mainwindow.h \
        CustomCommandDialog.h \
        CommandEditDialog.h

FORMS += \
        mainwindow.ui

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
