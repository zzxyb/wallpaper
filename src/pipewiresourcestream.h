#ifndef PIPEWIRESOURCESTREAM_H
#define PIPEWIRESOURCESTREAM_H

#include "wallpaperglobal.h"

#include <optional>

#include <pipewire/pipewire.h>
#include <spa/param/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/video/format-utils.h>

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QSharedPointer>
#include <QSize>

struct DmaBufPlane {
    int fd;
    uint32_t offset;
    uint32_t stride;
};

struct DmaBufAttributes {
    int width = 0;
    int height = 0;
    uint32_t format = 0;
    uint64_t modifier = 0;

    QVector<DmaBufPlane> planes;
};

struct PipeWireCursor {
    QPoint position;
    QPoint hotspot;
    QImage texture;
};

struct PipeWireFrame {
    spa_video_format format;
    int sequential;
    qint64 presentationTimestamp;
    std::optional<DmaBufAttributes> dmabuf;
    std::optional<QImage> image;
    std::optional<QRegion> damage;
    std::optional<PipeWireCursor> cursor;
};

struct Fraction {
    const quint32 numerator;
    const quint32 denominator;
};

class PipewireSourceStreamPrivate;

class WSM_WALLPAPER_EXPORT PipewireSourceStream : public QObject
{
    Q_OBJECT
public:
    explicit PipewireSourceStream(QObject *parent = nullptr);
    ~PipewireSourceStream() override;

    Fraction framerate() const;
    uint nodeId();
    QString error() const;

    QSize size() const;
    bool createStream(uint nodeid, int fd);
    void setActive(bool active);
    void setDamageEnabled(bool withDamage);

    void handleFrame(struct pw_buffer *buffer);
    void process();
    void renegotiateModifierFailed(spa_video_format format, quint64 modifier);
    qint64 currentPresentationTimestamp() const;
    static uint32_t spaVideoFormatToDrmFormat(spa_video_format spa_format);

    bool allowDmaBuf();
    bool withDamage();
    pw_stream *pwStream();
    bool stopped();
    spa_video_info_raw videoFormat() const;

Q_SIGNALS:
    void streamReady();
    void startStreaming();
    void stopStreaming();
    void streamParametersChanged();
    void frameReceived(const PipeWireFrame &frame);

private:
    static void onStreamParamChanged(void *data, uint32_t id, const struct spa_pod *format);
    static void onStreamStateChanged(void *data, pw_stream_state old, pw_stream_state state, const char *error_message);
    static void onRenegotiate(void *data, uint64_t);
    QVector<const spa_pod *> createFormatsParams();

    void coreFailed(const QString &errorMessage);

private:
    Q_DECLARE_PRIVATE(PipewireSourceStream)
    Q_DISABLE_COPY(PipewireSourceStream)
};

Q_DECLARE_METATYPE(QVector<DmaBufPlane>);

#endif // PIPEWIRESOURCESTREAM_H
