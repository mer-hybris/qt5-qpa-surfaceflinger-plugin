/****************************************************************************
**
** Copyright (C) 2018 Jolla Ltd.
** Contact: Thomas Perl <thomas.perl@jolla.com>
**
** Based on eglfshwcintegration.cpp
**
** Copyright (C) 2016 Michael Serpieri <mickybart@pygoscelis.org>
**
** This file is part of the surfaceflinger plugin.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "eglfssurfaceflingerintegration.h"

#include <private/qeglfswindow_p.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>

#include <inttypes.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sync/sync.h>

#include <android-config.h>
#include <hybris/surface_flinger/surface_flinger_compatibility_layer.h>
#include <hardware/hwcomposer_defs.h>
#include "hybris_nativebufferext.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <qdebug.h>


// Evaluate "x", if it doesn't return zero, print a warning
#define SF_PLUGIN_EXPECT_ZERO(x) \
    { int res; if ((res = (x)) != 0) \
        qCWarning(QPA_LOG_SF, "%s in %s returned %i", (#x), __func__, res); }

// Evaluate "x", if it isn't NULL, print a warning
#define SF_PLUGIN_EXPECT_NULL(x) \
    { void *res; if ((res = (x)) != NULL) \
        qCWarning(QPA_LOG_SF, "%s in %s returned %x", (#x), __func__, (unsigned int)res); }

// Evaluate "x", if it is NULL, exit with a fatal error
#define SF_PLUGIN_FATAL(x) \
    qFatal("QPA-SF: %s in %s", x, __func__)

// Evaluate "x", if it is NULL, exit with a fatal error
#define SF_PLUGIN_ASSERT_NOT_NULL(x) \
    { void *res; if ((res = (x)) == NULL) \
        qFatal("QPA-SF: %s in %s returned %x", (#x), __func__, (unsigned int)res); }

// Evaluate "x", if it doesn't return zero, exit with a fatal error
#define SF_PLUGIN_ASSERT_ZERO(x) \
    { int res; if ((res = (x)) != 0) \
        qFatal("QPA-SF: %s in %s returned %i", (#x), __func__, res); }

static int exitSignalFd = -1;

static void exit_qt_gracefully(int sig)
{
    int64_t eventData = sig;
    const ssize_t size = ::write(exitSignalFd, &eventData, sizeof(eventData));
    Q_ASSERT(size == sizeof(eventData));
}

EglFsSurfaceFlingerIntegration::EglFsSurfaceFlingerIntegration()
    : m_exitNotifier(::eventfd(0, EFD_NONBLOCK), QSocketNotifier::Read)
{
    exitSignalFd = m_exitNotifier.socket();

    // We need to catch the SIGTERM and SIGINT signals, so that we can do a
    // proper shutdown of Qt and the plugin, and avoid crashes, hangs and
    // reboots in cases where we don't properly close the surfaceflinger.
    struct sigaction new_action;
    new_action.sa_handler = exit_qt_gracefully;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGTERM, &new_action, NULL);
    sigaction(SIGINT, &new_action, NULL);

    QObject::connect(&m_exitNotifier, &QSocketNotifier::activated, []() {
        int64_t eventData = 0;
        const ssize_t size = ::read(exitSignalFd, &eventData, sizeof(eventData));
        Q_ASSERT(size == sizeof(eventData));

        QCoreApplication::exit(0);
    });

}

EglFsSurfaceFlingerIntegration::~EglFsSurfaceFlingerIntegration()
{
    if (exitSignalFd >= 0) {
        ::close(exitSignalFd);
        exitSignalFd = 0;
    }
}

void EglFsSurfaceFlingerIntegration::platformInit()
{
    // This actually opens the surfaceflinger device
    sf_client = sf_client_create();
    SF_PLUGIN_ASSERT_NOT_NULL(sf_client);
}

void EglFsSurfaceFlingerIntegration::platformDestroy()
{
    // TODO: Add support to release resources
    sf_blank(0);

    sf_client = nullptr;
}

EGLNativeDisplayType EglFsSurfaceFlingerIntegration::platformDisplay() const
{
    return sf_client_get_egl_display(sf_client);
}

EGLDisplay EglFsSurfaceFlingerIntegration::createDisplay(EGLNativeDisplayType nativeDisplay)
{
    return QEglFSDeviceIntegration::createDisplay(nativeDisplay);
}

bool EglFsSurfaceFlingerIntegration::usesDefaultScreen()
{
    return QEglFSDeviceIntegration::usesDefaultScreen();
}

void EglFsSurfaceFlingerIntegration::screenInit()
{
    QEglFSDeviceIntegration::screenInit();
}

void EglFsSurfaceFlingerIntegration::screenDestroy()
{
    QEglFSDeviceIntegration::screenDestroy();
}

QSizeF EglFsSurfaceFlingerIntegration::physicalScreenSize() const
{
    return info.physicalScreenSize();
}

QSize EglFsSurfaceFlingerIntegration::screenSize() const
{
    return info.screenSize();
}

QDpi EglFsSurfaceFlingerIntegration::logicalDpi() const
{
    return QEglFSDeviceIntegration::logicalDpi();
}

qreal EglFsSurfaceFlingerIntegration::pixelDensity() const
{
    return QEglFSDeviceIntegration::pixelDensity();
}

Qt::ScreenOrientation EglFsSurfaceFlingerIntegration::nativeOrientation() const
{
    return QEglFSDeviceIntegration::nativeOrientation();
}

Qt::ScreenOrientation EglFsSurfaceFlingerIntegration::orientation() const
{
    return QEglFSDeviceIntegration::orientation();
}

int EglFsSurfaceFlingerIntegration::screenDepth() const
{
    return info.screenDepth();
}

QImage::Format EglFsSurfaceFlingerIntegration::screenFormat() const
{
    switch (info.screenDepth()) {
        case 16:
            return QImage::Format_RGB16;
        default:
            return QImage::Format_RGB32;
    }
}

qreal EglFsSurfaceFlingerIntegration::refreshRate() const
{
    return info.refreshRate();
}

QSurfaceFormat EglFsSurfaceFlingerIntegration::surfaceFormatFor(const QSurfaceFormat &inputFormat) const
{
    QSurfaceFormat newFormat = inputFormat;
    if (screenDepth() == 16) {
        newFormat.setRedBufferSize(5);
        newFormat.setGreenBufferSize(6);
        newFormat.setBlueBufferSize(5);
    } else {
        newFormat.setStencilBufferSize(8);
        newFormat.setAlphaBufferSize(8);
        newFormat.setRedBufferSize(8);
        newFormat.setGreenBufferSize(8);
        newFormat.setBlueBufferSize(8);
    }

    return newFormat;
}

EGLint EglFsSurfaceFlingerIntegration::surfaceType() const
{
    return QEglFSDeviceIntegration::surfaceType();
}

QEglFSWindow *EglFsSurfaceFlingerIntegration::createWindow(QWindow *window) const
{
    return QEglFSDeviceIntegration::createWindow(window);
}

EGLNativeWindowType EglFsSurfaceFlingerIntegration::createNativeWindow(
        QPlatformWindow *platformWindow, const QSize &size, const QSurfaceFormat &format)
{
    Q_UNUSED(platformWindow);
    Q_UNUSED(format);

    if (sf_surface) {
        SF_PLUGIN_FATAL("There can only be one window, someone tried to create more.");
    }

    SfSurfaceCreationParameters params = {
            0,
            0,
            size.width(),
            size.height(),
            HYBRIS_PIXEL_FORMAT_RGBA_8888, //PIXEL_FORMAT_RGBA_8888
            INT_MAX, //layer position
            1.0f, //opacity
            0, //create_egl_window_surface
            "qt5-qpa-plugin"
    };

    sf_surface = sf_surface_create(sf_client, &params);
    SF_PLUGIN_ASSERT_NOT_NULL(sf_surface);

    return sf_surface_get_egl_native_window(sf_surface);
}

EGLNativeWindowType EglFsSurfaceFlingerIntegration::createNativeOffscreenWindow(
        const QSurfaceFormat &format)
{
    return QEglFSDeviceIntegration::createNativeOffscreenWindow(format);
}

void EglFsSurfaceFlingerIntegration::destroyNativeWindow(EGLNativeWindowType window)
{
    Q_UNUSED(window);

    sf_surface = nullptr;
}

bool EglFsSurfaceFlingerIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap) {
        case QPlatformIntegration::ThreadedPixmaps:
        case QPlatformIntegration::OpenGL:
        case QPlatformIntegration::ThreadedOpenGL:
        case QPlatformIntegration::BufferQueueingOpenGL:
            return true;
        default:
            return QEglFSDeviceIntegration::hasCapability(cap);
    }
}

QPlatformCursor *EglFsSurfaceFlingerIntegration::createCursor(QPlatformScreen *screen) const
{
    return QEglFSDeviceIntegration::createCursor(screen);
}

bool EglFsSurfaceFlingerIntegration::filterConfig(EGLDisplay display, EGLConfig config) const
{
    return QEglFSDeviceIntegration::filterConfig(display, config);
}

void EglFsSurfaceFlingerIntegration::waitForVSync(QPlatformSurface *surface) const
{
    QEglFSDeviceIntegration::waitForVSync(surface);
}

bool EglFsSurfaceFlingerIntegration::swapBuffers(QPlatformSurface *surface)
{
    if (display_off) {
        qCWarning(QPA_LOG_SF, "Swap requested while display is off");
        return true;
    }
    return QEglFSDeviceIntegration::swapBuffers(surface);
}

void EglFsSurfaceFlingerIntegration::presentBuffer(QPlatformSurface *surface)
{
    QEglFSDeviceIntegration::presentBuffer(surface);
}

QByteArray EglFsSurfaceFlingerIntegration::fbDeviceName() const
{
    return QEglFSDeviceIntegration::fbDeviceName();
}

int EglFsSurfaceFlingerIntegration::framebufferIndex() const
{
    return QEglFSDeviceIntegration::framebufferIndex();
}

bool EglFsSurfaceFlingerIntegration::supportsPBuffers() const
{
    return QEglFSDeviceIntegration::supportsPBuffers();
}

bool EglFsSurfaceFlingerIntegration::supportsSurfacelessContexts() const
{
    return QEglFSDeviceIntegration::supportsSurfacelessContexts();
}

void *EglFsSurfaceFlingerIntegration::wlDisplay() const
{
    return QEglFSDeviceIntegration::wlDisplay();
}

void *EglFsSurfaceFlingerIntegration::nativeResourceForIntegration(const QByteArray &resource)
{
    const QByteArray lowerCaseResource = resource.toLower();

    if (lowerCaseResource == "displayoff") {
        // Called from lipstick to turn off the display (src/homeapplication.cpp)
        qCDebug(QPA_LOG_SF, "sleepDisplay");
        display_off = true;

        sf_blank(0);
    } else if (lowerCaseResource == "displayon") {
        // Called from lipstick to turn on the display (src/homeapplication.cpp)
        qCDebug(QPA_LOG_SF, "unsleepDisplay");
        display_off = false;

        sf_unblank(0);
    }

    return nullptr;

}
