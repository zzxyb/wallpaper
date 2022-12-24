#ifndef PIPEWIRESOURCESTREAM_P_H
#define PIPEWIRESOURCESTREAM_P_H

#include "wallpaperglobal.h"
#include "pipewiresourcestream.h"
#include "pipewirecore.h"

#include <private/qobject_p.h>

class WSM_WALLPAPER_EXPORT PipewireSourceStreamPrivate : public QObjectPrivate
{
    Q_DECLARE_PUBLIC(PipewireSourceStream)

public:
    PipewireSourceStreamPrivate()
    {
    }

    QSharedPointer<PipewireCore> pwCore;
    pw_stream *pwStream = nullptr;
    spa_hook streamListener;

    uint32_t pwNodeId = 0;
    bool stopped = false;

    spa_video_info_raw videoFormat;
    QString error;
    bool allowDmaBuf = true;
    qint64 currentPresentationTimestamp;

    QHash<spa_video_format, QVector<uint64_t>> availableModifiers;
    spa_source *renegotiateEvent = nullptr;

    bool withDamage = false;
};

#endif // PIPEWIRESOURCESTREAM_P_H
