# ------------------------------------------------------------
#
# IMOL (Infinite Modules On Line)
#
# @brief: Base widgets architecture framework
# @author: LuoQi
# @created at: 2019-06-14T15:40:08
#
# ------------------------------------------------------------

QT       -= gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = imolBwaf
TEMPLATE = lib

DEFINES += BWAF_LIBRARY

DEFINES += QT_DEPRECATED_WARNINGS

#user defines
DEFINES += MODULE_NAME=\\\"imol.ui\\\"
#DEFINES += LOG_ENABLE_DETAILS
#DEFINES += LOG_MIN_FILE_LEVEL=DEBUG
#end of user defines

INCLUDEPATH += $$TOP_SRCDIR/include/ \
    $$TOP_SRCDIR/include/bwaf/

LIBS += -limolCore

include(../../qmake/imol_config.pri)

SOURCES += \
    bfactory.cpp \
    custom/backgroundwidget.cpp \
    custom/spin.cpp \
    custom/touchbutton.cpp \
    custom/graphicsview.cpp \
    dockarea.cpp \
    topmenu.cpp \
    toolbar.cpp \
    bmodule.cpp \
    navbar.cpp \
    statusbar.cpp \
    centerwidget.cpp \
    sidebar.cpp \
    bunit.cpp \
    custom/enablebutton.cpp \
    custom/doublespin.cpp \

HEADERS += \
    ../../include/bwaf/bfactory.h \
    ../../include/bwaf/bmodule.h \
    ../../include/bwaf/bunit.h \
    ../../include/bwaf/bwaf_global.h \
    ../../include/bwaf/centerwidget.h \
    ../../include/bwaf/custom/backgroundwidget.h \
    ../../include/bwaf/custom/doublespin.h \
    ../../include/bwaf/custom/enablebutton.h \
    ../../include/bwaf/custom/spin.h \
    ../../include/bwaf/custom/touchbutton.h \
    ../../include/bwaf/custom/graphicsview.h \
    ../../include/bwaf/dockarea.h \
    ../../include/bwaf/navbar.h \
    ../../include/bwaf/sidebar.h \
    ../../include/bwaf/statusbar.h \
    ../../include/bwaf/toolbar.h \
    ../../include/bwaf/topmenu.h

unix {
    target.path = /usr/lib
    INSTALLS += target
}

FORMS += \
    navbar.ui
