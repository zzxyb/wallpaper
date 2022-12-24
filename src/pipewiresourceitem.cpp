#include "eglhelpers.h"
#include "pipewiresourceitem.h"
#include "private/pipewiresourceitem_p.h"

#include <EGL/eglext.h>

#include <fcntl.h>
#include <libdrm/drm_fourcc.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOpenGLTexture>
#include <QSocketNotifier>
#include <QVersionNumber>
#include <QThread>
#include <qpa/qplatformnativeinterface.h>

class PipeWireRenderNode : public QSGNode
{
public:
    QSGImageNode *screenNode(QQuickWindow *window)
    {
        if (!m_screenNode) {
            m_screenNode = window->createImageNode();
            appendChildNode(m_screenNode);
        }
        return m_screenNode;
    }
    QSGImageNode *cursorNode(QQuickWindow *window)
    {
        if (!m_cursorNode) {
            m_cursorNode = window->createImageNode();
            appendChildNode(m_cursorNode);
        }
        return m_cursorNode;
    }

    QSGImageNode *damageNode(QQuickWindow *window)
    {
        if (!m_damageNode) {
            m_damageNode = window->createImageNode();
            appendChildNode(m_damageNode);
        }
        return m_damageNode;
    }

    void discardCursor()
    {
        if (m_cursorNode) {
            removeChildNode(m_cursorNode);
            delete m_cursorNode;
            m_cursorNode = nullptr;
        }
    }

    void discardDamage()
    {
        if (m_damageNode) {
            removeChildNode(m_damageNode);
            delete m_damageNode;
            m_damageNode = nullptr;
        }
    }

private:
    QSGImageNode *m_screenNode = nullptr;
    QSGImageNode *m_cursorNode = nullptr;
    QSGImageNode *m_damageNode = nullptr;
};

class DiscardEglPixmapRunnable : public QRunnable
{
public:
    DiscardEglPixmapRunnable(EGLImageKHR image, QOpenGLTexture *texture)
        : m_image(image)
        , m_texture(texture)
    {
    }

    void run() override
    {
        if (m_image != EGL_NO_IMAGE_KHR) {
            eglDestroyImage(eglGetCurrentDisplay(), m_image);
        }

        delete m_texture;
    }

private:
    const EGLImageKHR m_image;
    QOpenGLTexture *m_texture;
};

PipewireSourceItem::PipewireSourceItem(QQuickItem *parent)
    : QQuickItem(*(new PipewireSourceItemPrivate), parent)
{
    setFlag(ItemHasContents, true);

    connect(this, &QQuickItem::visibleChanged, this, &PipewireSourceItem::handleVisibleChanged);
}

void PipewireSourceItem::setNodeId(uint nodeId)
{
    Q_D(PipewireSourceItem);

    if (nodeId == d->nodeId)
        return;

    d->nodeId = nodeId;
    refresh();
    Q_EMIT nodeIdChanged(nodeId);
}

uint PipewireSourceItem::nodeId() const
{
    Q_D(const PipewireSourceItem);
    return d->nodeId;
}

void PipewireSourceItem::setFd(uint fd)
{
    Q_D(PipewireSourceItem);

    if (fd == d->fd)
        return;

    d->fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    refresh();
    Q_EMIT fdChanged(fd);
}

uint PipewireSourceItem::fd() const
{

}

PipewireSourceItem::PipewireSourceItem(PipewireSourceItemPrivate &dd, QQuickItem *parent)
    : QQuickItem(dd, parent)
{}

void PipewireSourceItem::handleVisibleChanged()
{
    Q_D(PipewireSourceItem);

    setEnabled(isVisible());
    if (d->stream)
        d->stream->setActive(isVisible());
}

void PipewireSourceItem::componentComplete()
{
    Q_D(const PipewireSourceItem);

    QQuickItem::componentComplete();
    if (d->nodeId != 0) {
        refresh();
    }
}

void PipewireSourceItem::releaseResources()
{
    Q_D(PipewireSourceItem);

    if (window()) {
        window()->scheduleRenderJob(new DiscardEglPixmapRunnable(d->image, d->texture.take()), QQuickWindow::NoStage);
        d->image = EGL_NO_IMAGE_KHR;
    }
}

QSGNode *PipewireSourceItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *data)
{
    Q_D(PipewireSourceItem);

    if (Q_UNLIKELY(!d->createNextTexture)) {
        return node;
    }

    auto texture = d->createNextTexture;
    if (!texture) {
        delete node;
        return nullptr;
    }

    QSGImageNode *screenNode;
    auto pwNode = dynamic_cast<PipeWireRenderNode *>(node);
    if (!pwNode) {
        delete node;
        pwNode = new PipeWireRenderNode;
        screenNode = window()->createImageNode();
        screenNode->setOwnsTexture(true);
        pwNode->appendChildNode(screenNode);
    } else {
        screenNode = static_cast<QSGImageNode *>(pwNode->childAtIndex(0));
    }
    screenNode->setTexture(texture);

    const auto br = boundingRect().toRect();
    QRect rect({0, 0}, texture->textureSize().scaled(br.size(), Qt::KeepAspectRatio));
    rect.moveCenter(br.center());
    screenNode->setRect(rect);

    if (d->cursor.position.isNull() || d->cursor.texture.isNull()) {
        pwNode->discardCursor();
    } else {
        QSGImageNode *cursorNode = pwNode->cursorNode(window());
        if (d->cursor.dirty) {
            cursorNode->setTexture(window()->createTextureFromImage(d->cursor.texture));
            d->cursor.dirty = false;
        }
        const qreal scale = qreal(rect.width()) / texture->textureSize().width();
        cursorNode->setRect(QRectF{rect.topLeft() + (d->cursor.position * scale), d->cursor.texture.size() * scale});
        Q_ASSERT(cursorNode->texture());
    }

    if (d->damage.isNull() || d->damage.isEmpty()) {
        pwNode->discardDamage();
    } else {
        auto *damageNode = pwNode->damageNode(window());
        QImage damageImage(texture->textureSize(), QImage::Format_RGBA64_Premultiplied);
        damageImage.fill(Qt::transparent);
        QPainter p(&damageImage);
        p.setBrush(Qt::red);
        for (auto rect : d->damage) {
            p.drawRect(rect);
        }
        damageNode->setTexture(window()->createTextureFromImage(damageImage));
        damageNode->setRect(rect);
        Q_ASSERT(damageNode->texture());
    }
    return pwNode;
}

void PipewireSourceItem::itemChange(ItemChange change, const ItemChangeData &data)
{
    Q_D(PipewireSourceItem);

    switch (change) {
    case ItemVisibleHasChanged:
        setEnabled(isVisible());
        if (d->stream)
            d->stream->setActive(isVisible() && data.boolValue && isComponentComplete());
        break;
    case ItemSceneChange:
        d->needsRecreateTexture = true;
        releaseResources();
        break;
    default:
        break;
    }
}

void PipewireSourceItem::processFrame(const PipeWireFrame &frame)
{
    Q_D(PipewireSourceItem);

    d->damage = frame.damage.value();

    if (frame.cursor) {
        d->cursor.position = frame.cursor->position;
        d->cursor.hotspot = frame.cursor->hotspot;
        if (!frame.cursor->texture.isNull()) {
            d->cursor.dirty = true;
            d->cursor.texture = frame.cursor->texture;
        }
    }

    if (frame.dmabuf) {
        updateTextureDmaBuf(frame.dmabuf.value(), frame.format);
    } else if (frame.image) {
        updateTextureImage(frame.image.value());
    }

    if (window() && window()->isVisible()) {
        update();
    }
}

void PipewireSourceItem::updateTextureDmaBuf(const DmaBufAttributes &attribs, spa_video_format format)
{
    Q_D(PipewireSourceItem);

    if (!window()) {
        qWarning() << "Window not available" << this;
        return;
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    const auto openglContext = window()->openglContext();
#else
    const auto openglContext = static_cast<QOpenGLContext *>(window()->rendererInterface()->getResource(window(), QSGRendererInterface::OpenGLContextResource));
#endif
    if (!openglContext || !d->stream) {
        qWarning() << "need a window and a context" << window();
        return;
    }

    const EGLDisplay display = static_cast<EGLDisplay>(QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("egldisplay"));
    if (d->image) {
        eglDestroyImage(display, d->image);
    }
    const auto size = d->stream->size();
    const EGLContext context = static_cast<EGLContext>(QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("eglcontext"));
    d->image = EGLHelpers::createImage(display, context, attribs, PipewireSourceStream::spaVideoFormatToDrmFormat(format), size);
    if (d->image == EGL_NO_IMAGE_KHR) {
        d->stream->renegotiateModifierFailed(format, attribs.modifier);
        d->createNextTexture = nullptr;
    }
    if (!d->texture) {
        d->texture.reset(new QOpenGLTexture(QOpenGLTexture::Target2D));
        bool created = d->texture->create();
        Q_ASSERT(created);
    }

    d->texture->bind();

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)d->image);

    d->texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    d->texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
    d->texture->release();
    d->texture->setSize(size.width(), size.height());

    int textureId = d->texture->textureId();
    QQuickWindow::CreateTextureOption textureOption =
            format == SPA_VIDEO_FORMAT_ARGB || format == SPA_VIDEO_FORMAT_BGRA ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::TextureIsOpaque;
    setEnabled(true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    d->createNextTexture = window()->createTextureFromNativeObject(QQuickWindow::NativeObjectTexture, &textureId, 0 /*a vulkan thing?*/, size, textureOption);
#else
    d->createNextTexture = QNativeInterface::QSGOpenGLTexture::fromNative(textureId, window(), size, textureOption);
#endif
}

void PipewireSourceItem::updateTextureImage(const QImage &image)
{
    Q_D(PipewireSourceItem);

    if (!window()) {
        qWarning() << "pass";
        return;
    }

    d->createNextTexture = window()->createTextureFromImage(image, QQuickWindow::TextureIsOpaque);
}

void PipewireSourceItem::refresh()
{
    Q_D(PipewireSourceItem);

    setEnabled(false);

    if (!isComponentComplete()) {
        return;
    }

    if (d->nodeId == 0) {
        d->stream.reset(nullptr);
        d->createNextTexture = nullptr;
    } else {
        d->stream.reset(new PipewireSourceStream(this));
        d->stream->createStream(d->nodeId, d->fd);
        if (!d->stream->error().isEmpty()) {
            d->stream.reset(nullptr);
            d->nodeId = 0;
            return;
        }
        d->stream->setActive(isVisible() && isComponentComplete());

        connect(d->stream.data(), &PipewireSourceStream::frameReceived, this, &PipewireSourceItem::processFrame);
    }
}
