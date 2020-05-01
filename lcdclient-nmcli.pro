TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle

QT += network

SOURCES += main.cpp \
    LcdClient.cpp

HEADERS += \
    LcdClient.hpp

DISTFILES += \
    README.md \
    LICENSE
