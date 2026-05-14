QT += core gui widgets
CONFIG += c++17

# Apple Clang + recent SDK: QtCore's qyieldcpu.h calls __yield() without pulling in
# <arm_acle.h>, which triggers -Wimplicit-function-declaration-as-error.
QMAKE_CXXFLAGS += -Wno-error=implicit-function-declaration

SOURCES += main.cpp \
           CarItem.cpp \
           DirectionalLight.cpp \
           HashMap.cpp \
           Hospital.cpp \
           HospitalManager.cpp \
           IntersectionWindow.cpp \
           Lane.cpp \
           PriorityQueue.cpp \
           TrafficController.cpp \
           edge.cpp \
           graphmanager.cpp \
           graphnode.cpp \
           randomroutegenerator.cpp

RESOURCES += resources.qrc

HEADERS += CarItem.h \
           HashMap.h \
           Hospital.h \
           HospitalManager.h \
           IntersectionWindow.h \
           node.h \
           PriorityQueue.h \
           Lane.h \
           TrafficController.h \
           Trafficlight.h \
           DirectionalLight.h \
           edge.h \
           graphinfo.h \
           graphmanager.h \
           graphnode.h \
           randomroutegenerator.h
