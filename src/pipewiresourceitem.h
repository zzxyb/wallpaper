#ifndef PIPEWIRESOURCEITEM_H
#define PIPEWIRESOURCEITEM_H

#include "wallpaperglobal.h"
#include "pipewiresourcestream.h"

#include <QQuickItem>

class  PipewireSourceItemPrivate;
class WSM_WALLPAPER_EXPORT PipewireSourceItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(uint nodeId READ nodeId WRITE setNodeId NOTIFY nodeIdChanged)
    Q_PROPERTY(uint fd READ fd WRITE setFd NOTIFY fdChanged)
    QML_ELEMENT
public:
    PipewireSourceItem(QQuickItem *parent=nullptr);

    QString error() const;

    void setNodeId(uint nodeId);
    uint nodeId() const;

    void setFd(uint fd);
    uint fd() const;

    void componentComplete() override;
    void releaseResources() override;
Q_SIGNALS:
    void nodeIdChanged(uint nodeId);
    void fdChanged(uint fd);

protected:
    PipewireSourceItem(PipewireSourceItemPrivate &dd, QQuickItem *parent);
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;

private:
    void refresh();
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void processFrame(const PipeWireFrame &frame);
    void updateTextureDmaBuf(const DmaBufAttributes &attribs, spa_video_format format);
    void updateTextureImage(const QImage &image);

private Q_SLOTS:
    void handleVisibleChanged();

private:
    Q_DECLARE_PRIVATE(PipewireSourceItem)
    Q_DISABLE_COPY(PipewireSourceItem)
};

#endif // PIPEWIRESOURCEITEM_H
