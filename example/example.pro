TARGET = example
QT += qml quick

SOURCES += main.cpp
RESOURCES += qml.qrc

OUT_PWD_PATH = $$OUT_PWD
QML_IMPORT_PATH = $$replace(OUT_PWD_PATH, example, src)

qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
