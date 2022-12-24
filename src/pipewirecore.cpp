#include "pipewirecore.h"

#include <spa/utils/result.h>

#include <QSocketNotifier>
#include <QThread>
#include <QThreadStorage>
#include <QSharedPointer>
#include <QDebug>

pw_core_events PipewireCore::s_pwCoreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .info = &PipewireCore::onCoreInfo,
    .done = nullptr,
    .ping = nullptr,
    .error = &PipewireCore::onCoreError,
    .remove_id = nullptr,
    .bound_id = nullptr,
    .add_mem = nullptr,
    .remove_mem = nullptr,
};

PipewireCore::PipewireCore(QObject *parent)
    : QObject(parent)
{
    static std::once_flag pwInitOnce;
    std::call_once(pwInitOnce, [] { pw_init(nullptr, nullptr); });
}

PipewireCore::~PipewireCore()
{
    if (m_pwMainLoop) {
        pw_loop_leave(m_pwMainLoop);
    }

    if (m_pwCore) {
        pw_core_disconnect(m_pwCore);
    }

    if (m_pwContext) {
        pw_context_destroy(m_pwContext);
    }

    if (m_pwMainLoop) {
        pw_loop_destroy(m_pwMainLoop);
    }
}

void PipewireCore::onCoreError(void *data, uint32_t id, int seq, int res, const char *message)
{
    Q_UNUSED(seq)

    qDebug() << "PipeWire remote error: " << res << message;
    if (id == PW_ID_CORE) {
        PipewireCore *pw = static_cast<PipewireCore *>(data);
        Q_EMIT pw->pipewireFailed(QString::fromUtf8(message));
    }
}

void PipewireCore::onCoreInfo(void *data, const pw_core_info *info)
{
    PipewireCore *pw = static_cast<PipewireCore *>(data);
    pw->m_serverVersion = QVersionNumber::fromString(QString::fromUtf8(info->version));
}

bool PipewireCore::init(int fd)
{
    m_pwMainLoop = pw_loop_new(nullptr);
    pw_loop_enter(m_pwMainLoop);

    QSocketNotifier *notifier = new QSocketNotifier(pw_loop_get_fd(m_pwMainLoop), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
        int result = pw_loop_iterate(m_pwMainLoop, 0);
        if (result < 0)
            qDebug() << "pipewire_loop_iterate failed: " << spa_strerror(result);
    });

    m_pwContext = pw_context_new(m_pwMainLoop, nullptr, 0);
    if (!m_pwContext) {
        qDebug() << "Failed to create PipeWire context";
        m_error = QString("Failed to create PipeWire context");
        return false;
    }

    if (fd > 0) {
        m_pwCore = pw_context_connect_fd(m_pwContext, fd, nullptr, 0);
    } else {
        m_pwCore = pw_context_connect(m_pwContext, nullptr, 0);
    }
    if (!m_pwCore) {
        m_error = QString("Failed to connect to PipeWire");
        qDebug() << "error:" << m_error << fd;
        return false;
    }

    if (pw_loop_iterate(m_pwMainLoop, 0) < 0) {
        qDebug() << "Failed to start main PipeWire loop";
        m_error = QString("Failed to start main PipeWire loop");
        return false;
    }

    pw_core_add_listener(m_pwCore, &m_coreListener, &s_pwCoreEvents, this);
    return true;
}

QString PipewireCore::error() const
{
    return m_error;
}

QSharedPointer<PipewireCore> PipewireCore::fetch(int fd)
{
    static QThreadStorage<QHash<int, QWeakPointer<PipewireCore>>> global;
    QSharedPointer<PipewireCore> ret = global.localData().value(fd).toStrongRef();
    if (!ret) {
        ret.reset(new PipewireCore);
        if (ret->init(fd)) {
            global.localData().insert(fd, ret);
        }
    }
    return ret;
}
