TEMPLATE = subdirs
CONFIG += ordered
SUBDIRS +=\
         src \
         example \

OTHER_FILES += \
    README.md

examples.depends = src
