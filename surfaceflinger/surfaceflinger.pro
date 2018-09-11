TARGET = surfaceflinger

QT += eglfsdeviceintegration-private

CONFIG += \
    egl \
    hide_symbols \
    link_pkgconfig

# libhybris / droid integration
PKGCONFIG += \
    android-headers \
    libsf \
    hybris-egl-platform

# Avoid X11 header collision
DEFINES += \
    MESA_EGL_NO_X11_HEADERS \
    QEGL_EXTRA_DEBUG

QMAKE_LFLAGS += $$QMAKE_LFLAGS_NOUNDEF

SOURCES += \
    eglfssurfaceflingerintegration.cpp \
    main.cpp \
    surfaceflinger_screeninfo.cpp

HEADERS += \
    eglfssurfaceflingerintegration.h \
    surfaceflinger_screeninfo.h


PLUGIN_TYPE = egldeviceintegrations
PLUGIN_CLASS_NAME = EglFsSurfaceFlingerIntegrationPlugin
load(qt_plugin)
