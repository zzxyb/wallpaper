#include "pipewirecore.h"
#include "pipewiresourcestream.h"
#include "private/pipewiresourcestream_p.h"

#include <fcntl.h>
#include <libdrm/drm_fourcc.h>
#include <spa/utils/result.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <QDateTime>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QOpenGLTexture>
#include <QSocketNotifier>
#include <QVersionNumber>
#include <QThread>
#include <qpa/qplatformnativeinterface.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtPlatformHeaders/QEGLNativeContext>
#endif
#undef Status

#if !PW_CHECK_VERSION(0, 3, 29)
#define SPA_POD_PROP_FLAG_MANDATORY (1u << 3)
#endif
#if !PW_CHECK_VERSION(0, 3, 33)
#define SPA_POD_PROP_FLAG_DONT_FIXATE (1u << 4)
#endif

#define CURSOR_BPP 4
#define CURSOR_META_SIZE(w, h) (sizeof(struct spa_meta_cursor) + sizeof(struct spa_meta_bitmap) + w * h * CURSOR_BPP)

pw_stream_events pwStreamEvents = {};

static const QVersionNumber pwClientVersion = QVersionNumber::fromString(QString::fromUtf8(pw_get_library_version()));
static const QVersionNumber kDmaBufMinVersion = {0, 3, 24};
static const QVersionNumber kDmaBufModifierMinVersion = {0, 3, 33};
static const QVersionNumber kDropSingleModifierMinVersion = {0, 3, 40};
static const int videoDamageRegionCount = 16;

static QImage::Format SpaToQImageFormat(quint32 format)
{
    switch (format) {
    case SPA_VIDEO_FORMAT_BGR:
        return QImage::Format_BGR888;
    case SPA_VIDEO_FORMAT_RGBx:
        return QImage::Format_RGBX8888;
    case SPA_VIDEO_FORMAT_RGBA:
        return QImage::Format_RGBA8888_Premultiplied;
    default:
        return QImage::Format_RGB32;
    }
}

static void onProcess(void *data)
{
    PipewireSourceStream *stream = static_cast<PipewireSourceStream *>(data);
    stream->process();
}

static QHash<spa_video_format, QVector<uint64_t>> queryDmaBufModifiers(EGLDisplay display, const QVector<spa_video_format> &formats)
{
    QHash<spa_video_format, QVector<uint64_t>> ret;
    ret.reserve(formats.size());
    const char *extensionString = eglQueryString(display, EGL_EXTENSIONS);
    const bool hasEglImageDmaBufImportExt = strstr(extensionString, "EGL_EXT_image_dma_buf_import");
    static auto eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
    static auto eglQueryDmaBufFormatsEXT = (PFNEGLQUERYDMABUFFORMATSEXTPROC)eglGetProcAddress("eglQueryDmaBufFormatsEXT");

    EGLint count = 0;
    EGLBoolean successFormats = eglQueryDmaBufFormatsEXT(display, 0, nullptr, &count);

    QVector<uint32_t> drmFormats(count);
    successFormats &= eglQueryDmaBufFormatsEXT(display, count, reinterpret_cast<EGLint *>(drmFormats.data()), &count);
    if (!successFormats)
        qWarning() << "Failed to query DMA-BUF formats.";

    const QVector<uint64_t> mods = hasEglImageDmaBufImportExt ? QVector<uint64_t>{DRM_FORMAT_MOD_INVALID} : QVector<uint64_t>{};
    if (!eglQueryDmaBufFormatsEXT || !eglQueryDmaBufModifiersEXT || !hasEglImageDmaBufImportExt || !successFormats) {
        for (spa_video_format format : formats) {
            ret[format] = mods;
        }
        return ret;
    }

    for (spa_video_format format : formats) {
        uint32_t drm_format = PipewireSourceStream::spaVideoFormatToDrmFormat(format);
        if (drm_format == DRM_FORMAT_INVALID) {
            qDebug() << "Failed to find matching DRM format." << format;
            break;
        }

        if (std::find(drmFormats.begin(), drmFormats.end(), drm_format) == drmFormats.end()) {
            qDebug() << "Format " << drm_format << " not supported for modifiers.";
            ret[format] = mods;
            break;
        }

        successFormats = eglQueryDmaBufModifiersEXT(display, drm_format, 0, nullptr, nullptr, &count);
        if (!successFormats) {
            qWarning() << "Failed to query DMA-BUF modifier count.";
            ret[format] = mods;
            break;
        }

        QVector<uint64_t> modifiers(count);
        if (count > 0) {
            if (!eglQueryDmaBufModifiersEXT(display, drm_format, count, modifiers.data(), nullptr, &count)) {
                qWarning() << "Failed to query DMA-BUF modifiers.";
            }
        }

        // Support modifier-less buffers
        modifiers.push_back(DRM_FORMAT_MOD_INVALID);
        ret[format] = modifiers;
    }
    return ret;
}

static spa_pod *buildFormat(spa_pod_builder *builder, spa_video_format format, const QVector<uint64_t> &modifiers, bool withDontFixate)
{
    spa_pod_frame f[2];
    const spa_rectangle pw_min_screen_bounds{1, 1};
    const spa_rectangle pw_max_screen_bounds{UINT32_MAX, UINT32_MAX};

    spa_pod_builder_push_object(builder, &f[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(builder, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(builder, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
    spa_pod_builder_add(builder, SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&pw_min_screen_bounds, &pw_min_screen_bounds, &pw_max_screen_bounds), 0);

    if (modifiers.size() == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID) {
        // we only support implicit modifiers, use shortpath to skip fixation phase
        spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(builder, modifiers[0]);
    } else if (modifiers.size()) {
        // SPA_POD_PROP_FLAG_DONT_FIXATE can be used with PipeWire >= 0.3.33
        if (withDontFixate) {
            spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        } else {
            spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        }
        spa_pod_builder_push_choice(builder, &f[1], SPA_CHOICE_Enum, 0);
        // mofifiers from the array
        for (auto it = modifiers.begin(); it != modifiers.end(); it++) {
            spa_pod_builder_long(builder, *it);
            if (it == modifiers.begin()) {
                spa_pod_builder_long(builder, *it);
            }
        }
        spa_pod_builder_pop(builder, &f[1]);
    }

    return static_cast<spa_pod *>(spa_pod_builder_pop(builder, &f[0]));
}



PipewireSourceStream::PipewireSourceStream(QObject *parent)
    : QObject(*new PipewireSourceStreamPrivate, parent)
{
    qRegisterMetaType<QVector<DmaBufPlane>>();

    pwStreamEvents.version = PW_VERSION_STREAM_EVENTS;
    pwStreamEvents.process = &onProcess;
    pwStreamEvents.state_changed = &PipewireSourceStream::onStreamStateChanged;
    pwStreamEvents.param_changed = &PipewireSourceStream::onStreamParamChanged;
}

PipewireSourceStream::~PipewireSourceStream()
{
    Q_D(PipewireSourceStream);

    d->stopped = true;
    if (d->renegotiateEvent) {
        pw_loop_destroy_source(d->pwCore->loop(), d->renegotiateEvent);
    }
    if (d->pwStream) {
        pw_stream_destroy(d->pwStream);
    }
}

Fraction PipewireSourceStream::framerate() const
{
    Q_D(const PipewireSourceStream);
    if (d->pwStream) {
        return {d->videoFormat.max_framerate.num, d->videoFormat.max_framerate.denom};
    }

    return {0, 1};
}

uint PipewireSourceStream::nodeId()
{
    Q_D(PipewireSourceStream);

    return d->pwNodeId;
}

QString PipewireSourceStream::error() const
{
    Q_D(const PipewireSourceStream);

    return d->error;
}

QSize PipewireSourceStream::size() const
{
    Q_D(const PipewireSourceStream);

    return QSize(d->videoFormat.size.width, d->videoFormat.size.height);
}

bool PipewireSourceStream::createStream(uint nodeid, int fd)
{
    Q_D(PipewireSourceStream);

    d->availableModifiers.clear();
    d->pwCore = PipewireCore::fetch(fd);
    if (!d->pwCore->error().isEmpty()) {
        qDebug() << "received error while creating the stream" << d->pwCore->error();
        d->error = d->pwCore->error();
        return false;
    }

    connect(d->pwCore.data(), &PipewireCore::pipewireFailed, this, &PipewireSourceStream::coreFailed);

    if (objectName().isEmpty()) {
        setObjectName(QStringLiteral("plasma-screencast-%1").arg(nodeid));
    }

    const auto pwServerVersion = d->pwCore->serverVersion();
    d->pwStream = pw_stream_new(**d->pwCore, objectName().toUtf8().constData(), nullptr);
    d->pwNodeId = nodeid;
    pw_stream_add_listener(d->pwStream, &d->streamListener, &pwStreamEvents, this);

    d->renegotiateEvent = pw_loop_add_event(d->pwCore->loop(), onRenegotiate, this);

    QVector<const spa_pod *> params = createFormatsParams();
    pw_stream_flags s = (pw_stream_flags)(PW_STREAM_FLAG_DONT_RECONNECT | PW_STREAM_FLAG_AUTOCONNECT);
    if (pw_stream_connect(d->pwStream, PW_DIRECTION_INPUT, d->pwNodeId, s, params.data(), params.size()) != 0) {
        qDebug() << "Could not connect to stream";
        pw_stream_destroy(d->pwStream);
        d->pwStream = nullptr;
        return false;
    }
    qDebug() << "created successfully" << nodeid;
    return true;
}

void PipewireSourceStream::setActive(bool active)
{
    Q_D(PipewireSourceStream);

    Q_ASSERT(d->pwStream);
    pw_stream_set_active(d->pwStream, active);
}

void PipewireSourceStream::setDamageEnabled(bool withDamage)
{
    Q_D(PipewireSourceStream);

    d->withDamage = withDamage;
}

void PipewireSourceStream::handleFrame(pw_buffer *buffer)
{
    Q_D(PipewireSourceStream);

    spa_buffer *spaBuffer = buffer->buffer;

    PipeWireFrame frame;
    frame.format = d->videoFormat.format;

    struct spa_meta_header *h = (struct spa_meta_header *)spa_buffer_find_meta_data(spaBuffer, SPA_META_Header, sizeof(*h));
    if (h) {
        d->currentPresentationTimestamp = h->pts;
        frame.presentationTimestamp = h->pts;
        frame.sequential = h->seq;
    } else {
        d->currentPresentationTimestamp = QDateTime::currentDateTime().toMSecsSinceEpoch() * 1000000;
    }

    if (spa_meta *vd = spa_buffer_find_meta(spaBuffer, SPA_META_VideoDamage)) {
        frame.damage = QRegion();
        spa_meta_region *mr;
        spa_meta_for_each(mr, vd)
        {
            *frame.damage += QRect(mr->region.position.x, mr->region.position.y, mr->region.size.width, mr->region.size.height);
        }
    }

    { // process cursor
        struct spa_meta_cursor *cursor = static_cast<struct spa_meta_cursor *>(spa_buffer_find_meta_data(spaBuffer, SPA_META_Cursor, sizeof(*cursor)));
        if (spa_meta_cursor_is_valid(cursor)) {
            struct spa_meta_bitmap *bitmap = nullptr;

            if (cursor->bitmap_offset)
                bitmap = SPA_MEMBER(cursor, cursor->bitmap_offset, struct spa_meta_bitmap);

            QImage cursorTexture;
            if (bitmap && bitmap->size.width > 0 && bitmap->size.height > 0) {
                const uint8_t *bitmap_data = SPA_MEMBER(bitmap, bitmap->offset, uint8_t);
                cursorTexture = QImage(bitmap_data, bitmap->size.width, bitmap->size.height, bitmap->stride, SpaToQImageFormat(bitmap->format));
            }
            frame.cursor = {{cursor->position.x, cursor->position.y}, {cursor->hotspot.x, cursor->hotspot.y}, cursorTexture};
        } else {
            frame.cursor = {{}, {}, {}};
        }
    }

    if (spaBuffer->datas->chunk->size == 0) {
        // do not get a frame
    } else if (spaBuffer->datas->type == SPA_DATA_MemFd) {
        if (spaBuffer->datas->chunk->size == 0)
            return;

        uint8_t *map =
                static_cast<uint8_t *>(mmap(nullptr, spaBuffer->datas->maxsize + spaBuffer->datas->mapoffset, PROT_READ, MAP_PRIVATE, spaBuffer->datas->fd, 0));

        if (map == MAP_FAILED) {
            qDebug() << "Failed to mmap the memory: " << strerror(errno);
            return;
        }
        QImage img(map, d->videoFormat.size.width, d->videoFormat.size.height, spaBuffer->datas->chunk->stride, SpaToQImageFormat(d->videoFormat.format));
        frame.image = img.copy();

        munmap(map, spaBuffer->datas->maxsize + spaBuffer->datas->mapoffset);
    } else if (spaBuffer->datas->type == SPA_DATA_DmaBuf) {
        DmaBufAttributes attribs;
        attribs.planes.reserve(spaBuffer->n_datas);
        attribs.format = spaVideoFormatToDrmFormat(d->videoFormat.format);
        attribs.modifier = d->videoFormat.modifier;
        ;
        for (uint i = 0; i < spaBuffer->n_datas; ++i) {
            const auto &data = spaBuffer->datas[i];

            DmaBufPlane plane;
            plane.fd = data.fd;
            plane.stride = data.chunk->stride;
            plane.offset = data.chunk->offset;
            attribs.planes += plane;
        }
        Q_ASSERT(!attribs.planes.isEmpty());
        frame.dmabuf = attribs;
    } else if (spaBuffer->datas->type == SPA_DATA_MemPtr) {
        frame.image = QImage(static_cast<uint8_t *>(spaBuffer->datas->data),
                             d->videoFormat.size.width,
                             d->videoFormat.size.height,
                             spaBuffer->datas->chunk->stride,
                             SpaToQImageFormat(d->videoFormat.format));
    } else {
        if (spaBuffer->datas->type == SPA_ID_INVALID)
            qDebug() << "invalid buffer type";
        else
            qDebug() << "unsupported buffer type" << spaBuffer->datas->type;
        QImage errorImage(200, 200, QImage::Format_ARGB32_Premultiplied);
        errorImage.fill(Qt::red);
        frame.image = errorImage;
    }

    Q_EMIT frameReceived(frame);
}

void PipewireSourceStream::process()
{
    Q_D(PipewireSourceStream);

    pw_buffer *buf = pw_stream_dequeue_buffer(d->pwStream);
    if (!buf) {
        qDebug() << "out of buffers";
        return;
    }

    handleFrame(buf);

    pw_stream_queue_buffer(d->pwStream, buf);
}

void PipewireSourceStream::renegotiateModifierFailed(spa_video_format format, quint64 modifier)
{
    Q_D(PipewireSourceStream);

    if (d->pwCore->serverVersion() >= kDropSingleModifierMinVersion) {
        d->availableModifiers[format].removeAll(modifier);
    } else {
        d->allowDmaBuf = false;
    }
    qDebug() << "renegotiating, modifier didn't work" << format << modifier << "now only offering" << d->availableModifiers[format].count();
    pw_loop_signal_event(d->pwCore->loop(), d->renegotiateEvent);
}

qint64 PipewireSourceStream::currentPresentationTimestamp() const
{
    Q_D(const PipewireSourceStream);
    return d->currentPresentationTimestamp;
}

uint32_t PipewireSourceStream::spaVideoFormatToDrmFormat(spa_video_format spa_format)
{
    switch (spa_format) {
    case SPA_VIDEO_FORMAT_RGBA:
        return DRM_FORMAT_ABGR8888;
    case SPA_VIDEO_FORMAT_RGBx:
        return DRM_FORMAT_XBGR8888;
    case SPA_VIDEO_FORMAT_BGRA:
        return DRM_FORMAT_ARGB8888;
    case SPA_VIDEO_FORMAT_BGRx:
        return DRM_FORMAT_XRGB8888;
    case SPA_VIDEO_FORMAT_BGR:
        return DRM_FORMAT_BGR888;
    case SPA_VIDEO_FORMAT_RGB:
        return DRM_FORMAT_RGB888;
    default:
        qDebug() << "unknown format" << spa_format;
        return DRM_FORMAT_INVALID;
    }
}

bool PipewireSourceStream::allowDmaBuf()
{
    Q_D(const PipewireSourceStream);
    return d->allowDmaBuf;
}

bool PipewireSourceStream::withDamage()
{
    Q_D(const PipewireSourceStream);
    return d->withDamage;
}

pw_stream *PipewireSourceStream::pwStream()
{
    Q_D(PipewireSourceStream);
    return d->pwStream;
}

bool PipewireSourceStream::stopped()
{
    Q_D(const PipewireSourceStream);
    return d->stopped;
}

spa_video_info_raw PipewireSourceStream::videoFormat() const
{
    Q_D(const PipewireSourceStream);
    return d->videoFormat;
}

void PipewireSourceStream::onStreamParamChanged(void *data, uint32_t id, const spa_pod *format)
{
    if (!format || id != SPA_PARAM_Format) {
        return;
    }

    PipewireSourceStream *pw = static_cast<PipewireSourceStream *>(data);
    spa_video_info_raw video_info_raw = pw->videoFormat();
    spa_format_video_raw_parse(format, &video_info_raw);

    uint8_t paramsBuffer[1024];
    spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(paramsBuffer, sizeof(paramsBuffer));

    // When SPA_FORMAT_VIDEO_modifier is present we can use DMA-BUFs as
    // the server announces support for it.
    // See https://github.com/PipeWire/pipewire/blob/master/doc/dma-buf.dox

    const auto bufferTypes = pw->allowDmaBuf() && spa_pod_find_prop(format, nullptr, SPA_FORMAT_VIDEO_modifier)
            ? (1 << SPA_DATA_DmaBuf) | (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr)
            : (1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr);

    QVarLengthArray<const spa_pod *> params = {
        (spa_pod *)spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamBuffers,
        SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers,
        SPA_POD_CHOICE_RANGE_Int(16, 2, 16),
        SPA_PARAM_BUFFERS_align,
        SPA_POD_Int(16),
        SPA_PARAM_BUFFERS_dataType,
        SPA_POD_CHOICE_FLAGS_Int(bufferTypes)),
        (spa_pod *)spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamMeta,
        SPA_PARAM_Meta,
        SPA_PARAM_META_type,
        SPA_POD_Id(SPA_META_Header),
        SPA_PARAM_META_size,
        SPA_POD_Int(sizeof(struct spa_meta_header))),
        (spa_pod *)spa_pod_builder_add_object(&pod_builder,
        SPA_TYPE_OBJECT_ParamMeta,
        SPA_PARAM_Meta,
        SPA_PARAM_META_type,
        SPA_POD_Id(SPA_META_Cursor),
        SPA_PARAM_META_size,
        SPA_POD_CHOICE_RANGE_Int(CURSOR_META_SIZE(64, 64), CURSOR_META_SIZE(1, 1), CURSOR_META_SIZE(1024, 1024))),
    };

    if (pw->withDamage()) {
        params.append((spa_pod *)spa_pod_builder_add_object(&pod_builder,
                                                            SPA_TYPE_OBJECT_ParamMeta,
                                                            SPA_PARAM_Meta,
                                                            SPA_PARAM_META_type,
                                                            SPA_POD_Id(SPA_META_VideoDamage),
                                                            SPA_PARAM_META_size,
                                                            SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * videoDamageRegionCount,
                                                                                     sizeof(struct spa_meta_region) * 1,
                                                                                     sizeof(struct spa_meta_region) * videoDamageRegionCount)));
    }

    pw_stream_update_params(pw->pwStream(), params.data(), params.count());
    Q_EMIT pw->streamParametersChanged();
}

void PipewireSourceStream::onStreamStateChanged(void *data, pw_stream_state old, pw_stream_state state, const char *error_message)
{
    PipewireSourceStream *pw = static_cast<PipewireSourceStream *>(data);
    qDebug() << "state changed" << pw_stream_state_as_string(old) << "->" << pw_stream_state_as_string(state) << error_message;

    switch (state) {
    case PW_STREAM_STATE_ERROR:
        qWarning() << "Stream error: " << error_message;
        break;
    case PW_STREAM_STATE_PAUSED:
        Q_EMIT pw->streamReady();
        break;
    case PW_STREAM_STATE_STREAMING:
        Q_EMIT pw->startStreaming();
        break;
    case PW_STREAM_STATE_CONNECTING:
        break;
    case PW_STREAM_STATE_UNCONNECTED:
        if (!pw->stopped()) {
            Q_EMIT pw->stopStreaming();
        }
        break;
    }
}

void PipewireSourceStream::onRenegotiate(void *data, uint64_t)
{
    PipewireSourceStream *pw = static_cast<PipewireSourceStream *>(data);
    QVector<const spa_pod *> params = pw->createFormatsParams();
    pw_stream_update_params(pw->pwStream(), params.data(), params.size());
}

QVector<const spa_pod *> PipewireSourceStream::createFormatsParams()
{
    Q_D(PipewireSourceStream);

    const auto pwServerVersion = d->pwCore->serverVersion();
    uint8_t buffer[4096];
    spa_pod_builder podBuilder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const QVector<spa_video_format> formats =
    {SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGB, SPA_VIDEO_FORMAT_BGR};
    QVector<const spa_pod *> params;
    params.reserve(formats.size() * 2);
    const EGLDisplay display = static_cast<EGLDisplay>(QGuiApplication::platformNativeInterface()->nativeResourceForIntegration("egldisplay"));

    d->allowDmaBuf = pwServerVersion.isNull() || (pwClientVersion >= kDmaBufMinVersion && pwServerVersion >= kDmaBufMinVersion);
    const bool withDontFixate = pwServerVersion.isNull() || (pwClientVersion >= kDmaBufModifierMinVersion && pwServerVersion >= kDmaBufModifierMinVersion);

    if (d->availableModifiers.isEmpty()) {
        d->availableModifiers = queryDmaBufModifiers(display, formats);
    }

    for (auto it = d->availableModifiers.constBegin(), itEnd = d->availableModifiers.constEnd(); it != itEnd; ++it) {
        if (d->allowDmaBuf && !it->isEmpty()) {
            params += buildFormat(&podBuilder, it.key(), it.value(), withDontFixate);
        }

        params += buildFormat(&podBuilder, it.key(), {}, withDontFixate);
    }
    return params;
}

void PipewireSourceStream::coreFailed(const QString &errorMessage)
{
    Q_D(PipewireSourceStream);

    qDebug() << "received error message" << errorMessage;
    d->error = errorMessage;
    Q_EMIT stopStreaming();
}

