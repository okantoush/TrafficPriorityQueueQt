QT += core gui widgets
CONFIG += c++17

SOURCES += main.cpp \
           CarItem.cpp \
           IntersectionWindow.cpp \
           PriorityQueue.cpp

HEADERS += CarItem.h \
           HashMap.h \
           IntersectionWindow.h \
           Node.h \
           PriorityQueue.h \
           Lane.h \
           TrafficController.h \
           TrafficLight.h \
           DirectionalLight.h

DISTFILES += \
    Ambulance.png \
    Car1.png \
    Car2.png \
    Car3.png \
    firetruck.png \
    police car.png \
    police car.png
