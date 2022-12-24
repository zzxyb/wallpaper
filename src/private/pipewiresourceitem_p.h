#ifndef PIPEWIRESOURCEITEM_P_H
#define PIPEWIRESOURCEITEM_P_H

#include "wallpaperglobal.h"
#include "pipewiresourceitem.h"
#include "pipewiresourcestream.h"

#include <EGL/egl.h>

#include <private/qquickitem_p.h>

#include <QImage>
#include <QSGImageNode>
#include <QOpenGLTexture>
#include <QScopedPointer>

class WSM_WALLPAPER_EXPORT PipewireSourceItemPrivate : public QQuickItemPrivate
{
    Q_DECLARE_PUBLIC(PipewireSourceItem)

    struct Cursor{
        QImage texture;
        QPoint position;
        QPoint hotspot;
        bool dirty = false;
    };

public:
    PipewireSourceItemPrivate()
    {
    }

    uint nodeId = 0;
    uint fd = 0;

    QSGTexture *createNextTexture;
    QScopedPointer<PipewireSourceStream> stream;
    QScopedPointer<QOpenGLTexture> texture;

    EGLImage image = nullptr;
    bool needsRecreateTexture = false;

    Cursor cursor;
    QRegion damage;
};

#endif // PIPEWIRESOURCEITEM_P_H
