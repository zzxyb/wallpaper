#include "eglhelpers.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <libdrm/drm_fourcc.h>

#include <QDebug>
#include <QList>

#define ENUM_STRING(x) case x: return #x;

QByteArray EGLHelpers::formatEGLError(GLenum err)
{
    switch (err) {
    ENUM_STRING(EGL_SUCCESS)
            ENUM_STRING(EGL_BAD_DISPLAY)
            ENUM_STRING(EGL_BAD_CONTEXT)
            ENUM_STRING(EGL_BAD_PARAMETER)
            ENUM_STRING(EGL_BAD_MATCH)
            ENUM_STRING(EGL_BAD_ACCESS)
            ENUM_STRING(EGL_BAD_ALLOC)
            default:
        return QByteArray("0x") + QByteArray::number(err, 16);
    }
}

EGLImage EGLHelpers::createImage(EGLDisplay display, EGLContext context, const DmaBufAttributes &dmabufAttribs, uint32_t format, const QSize &size)
{
    const bool hasModifiers = dmabufAttribs.modifier != DRM_FORMAT_MOD_INVALID;

    QVector<EGLint> attribs;
    attribs << EGL_WIDTH << size.width() << EGL_HEIGHT << size.height() << EGL_LINUX_DRM_FOURCC_EXT << EGLint(format)

            << EGL_DMA_BUF_PLANE0_FD_EXT << dmabufAttribs.planes[0].fd << EGL_DMA_BUF_PLANE0_OFFSET_EXT << EGLint(dmabufAttribs.planes[0].offset)
            << EGL_DMA_BUF_PLANE0_PITCH_EXT << EGLint(dmabufAttribs.planes[0].stride);

    if (hasModifiers) {
        attribs << EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT << EGLint(dmabufAttribs.modifier & 0xffffffff) << EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT
                << EGLint(dmabufAttribs.modifier >> 32);
    }

    if (dmabufAttribs.planes.count() > 1) {
        attribs << EGL_DMA_BUF_PLANE1_FD_EXT << dmabufAttribs.planes[1].fd << EGL_DMA_BUF_PLANE1_OFFSET_EXT << EGLint(dmabufAttribs.planes[1].offset)
                << EGL_DMA_BUF_PLANE1_PITCH_EXT << EGLint(dmabufAttribs.planes[1].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT << EGLint(dmabufAttribs.modifier & 0xffffffff) << EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    if (dmabufAttribs.planes.count() > 2) {
        attribs << EGL_DMA_BUF_PLANE2_FD_EXT << dmabufAttribs.planes[2].fd << EGL_DMA_BUF_PLANE2_OFFSET_EXT << EGLint(dmabufAttribs.planes[2].offset)
                << EGL_DMA_BUF_PLANE2_PITCH_EXT << EGLint(dmabufAttribs.planes[2].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT << EGLint(dmabufAttribs.modifier & 0xffffffff) << EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    if (dmabufAttribs.planes.count() > 3) {
        attribs << EGL_DMA_BUF_PLANE3_FD_EXT << dmabufAttribs.planes[3].fd << EGL_DMA_BUF_PLANE3_OFFSET_EXT << EGLint(dmabufAttribs.planes[3].offset)
                << EGL_DMA_BUF_PLANE3_PITCH_EXT << EGLint(dmabufAttribs.planes[3].stride);

        if (hasModifiers) {
            attribs << EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT << EGLint(dmabufAttribs.modifier & 0xffffffff) << EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT
                    << EGLint(dmabufAttribs.modifier >> 32);
        }
    }

    attribs << EGL_NONE;

    static auto eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    EGLImage ret = eglCreateImageKHR(display, context, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer) nullptr, attribs.data());
    if (ret == EGL_NO_IMAGE_KHR) {
        qWarning() << "invalid image" << EGLHelpers::formatEGLError(eglGetError());
    }
    return ret;
}
