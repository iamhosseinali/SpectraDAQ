QT       += core gui widgets charts network

TARGET = SpectraDAQ
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

CONFIG += c++17
INCLUDEPATH += C:/local/boost_1_88_0
SOURCES += \
        main.cpp \
        mainwindow.cpp \
        CommandEditDialog.cpp \
        CustomCommandDialog.cpp \
        LoggingManager.cpp \
        FieldExtract.cpp

HEADERS += \
        mainwindow.h \
        CustomCommandDialog.h \
        LoggingManager.h \
        CommandEditDialog.h

FORMS += \
        mainwindow.ui

# Default rules for deployment
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
