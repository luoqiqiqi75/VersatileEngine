# ------------------------------------------------------------
#
# IMOL (Infinite Modules On Line)
#
# @brief: Library of basic usages, including module, creator...
# @author: LuoQi
# @created at: 2019-06-14T11:42:56
#
# ------------------------------------------------------------

QT -= gui

QT += network

TARGET = imolCore
TEMPLATE = lib

DEFINES += CORE_LIBRARY

DEFINES += QT_DEPRECATED_WARNINGS

INCLUDEPATH += $$TOP_SRCDIR/include/core/

include(../../qmake/imol_config.pri)

SOURCES += \
    commandmanager.cpp \
    modulemanager.cpp \
    logmanager.cpp \
    common.cpp \
    networkmanager.cpp \
    statemanager.cpp \
    translationmanager.cpp

HEADERS += \
    ../../include/core/commandmanager.h \
    ../../include/core/common.h \
    ../../include/core/core_global.h \
    ../../include/core/creatormanager.h \
    ../../include/core/logmanager.h \
    ../../include/core/modulemanager.h \
    ../../include/core/networkmanager.h \
    ../../include/core/statemanager.h \
    ../../include/core/translationmanager.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

RESOURCES += \
    asset.qrc
