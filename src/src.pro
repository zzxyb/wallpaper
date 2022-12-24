TEMPLATE = lib
TARGET = $$qtLibraryTarget(wallpaper)
QT += qml quick quick-private core core-private gui gui-private
CONFIG += plugin qmltypes c++17

CONFIG += link_pkgconfig
PKGCONFIG += egl libdrm libpipewire-0.3 libspa-0.2

TARGET = $$qtLibraryTarget($$TARGET)
uri = org.wsm.wallpaper

QML_IMPORT_NAME = org.wsm.wallpaper
QML_IMPORT_MAJOR_VERSION = 1.0

DESTDIR = $$replace(uri, \., /)

include(private/private.pri)

HEADERS += \
    eglhelpers.h \
    pipewirecore.h \
    pipewiresourceitem.h \
    pipewiresourcestream.h \
    wallpaperglobal.h \
    wallpaper_plugin.h \

SOURCES += \
    eglhelpers.cpp \
    pipewirecore.cpp \
    pipewiresourceitem.cpp \
    pipewiresourcestream.cpp \
    wallpaper_plugin.cpp \

DISTFILES += \
            qmldir \
            plugins.qmltypes \

system(doxygen ../doc/Doxyfile)

!equals(_PRO_FILE_PWD_, $$OUT_PWD) {
    copy_qmldir.target = $$OUT_PWD/qmldir
    copy_qmldir.depends = $$_PRO_FILE_PWD_/qmldir
    copy_qmldir.commands = $(COPY_FILE) "$$replace(copy_qmldir.depends, /, $$QMAKE_DIR_SEP)" "$$replace(copy_qmldir.target, /, $$QMAKE_DIR_SEP)"
    QMAKE_EXTRA_TARGETS += copy_qmldir
    PRE_TARGETDEPS += $$copy_qmldir.target
}

cpqmldir.files = qmldir
cpqmldir.path = $$DESTDIR
COPIES += cpqmldir

output_qmltypes_files = $$OUT_PWD/plugins.qmltypes
to_output_qmltypes = $$DESTDIR
to_pwd_qmltypes = $$PWD

QMAKE_PRE_LINK += cp $$output_qmltypes_files $$to_output_qmltypes && cp $$output_qmltypes_files $$to_pwd_qmltypes

qmldir.files = qmldir
typeinfo.files = plugins.qmltypes
unix {
    installPath = $$[QT_INSTALL_QML]/$$replace(uri, \., /)
    qmldir.path = $$installPath
    typeinfo.path = $$installPath
    target.path = $$installPath
    INSTALLS += target qmldir typeinfo
}
