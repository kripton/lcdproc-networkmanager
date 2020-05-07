TEMPLATE = app
CONFIG += console c++11 link_pkgconfig
CONFIG -= app_bundle

QT += network dbus

PKGCONFIG += libnm

INCLUDEPATH = $$[QT_SYSROOT]/usr/include/KF5/NetworkManagerQt
LIBS += -lKF5NetworkManagerQt

SOURCES += main.cpp \
    LcdClient.cpp

HEADERS += \
    LcdClient.hpp

DISTFILES += \
    README.md \
    LICENSE
