#ifndef EGLHELPERS_H
#define EGLHELPERS_H

#include "pipewiresourcestream.h"

#include <EGL/egl.h>

#include <QByteArray>

typedef unsigned int GLenum;

namespace EGLHelpers {
    QByteArray formatEGLError(GLenum err);

    EGLImage createImage(EGLDisplay display, EGLContext context, const DmaBufAttributes &dmabufAttribs, uint32_t format, const QSize &size);
}

#endif // EGLHELPERS_H
