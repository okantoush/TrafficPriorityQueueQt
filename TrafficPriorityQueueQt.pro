QT += core gui widgets
CONFIG += c++17

SOURCES += main.cpp \
           CarItem.cpp \
           IntersectionWindow.cpp \
           PriorityQueue.cpp

HEADERS += CarItem.h \
           IntersectionWindow.h \
           Node.h \
           PriorityQueue.h \
           Lane.h \
           TrafficController.h \
           TrafficLight.h \
           DirectionalLight.h
