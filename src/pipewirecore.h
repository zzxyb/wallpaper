#ifndef PIPEWIRECORE_H
#define PIPEWIRECORE_H

#include "wallpaperglobal.h"

#include <QObject>
#include <QVersionNumber>
#include <pipewire/pipewire.h>

class WSM_WALLPAPER_EXPORT PipewireCore : public QObject
{
    Q_OBJECT
public:
    explicit PipewireCore(QObject *parent = nullptr);
    ~PipewireCore() override;

    static void onCoreError(void *data, uint32_t id, int seq, int res, const char *message);
    static void onCoreInfo(void *data, const struct pw_core_info *info);

    bool init(int fd);
    QString error() const;
    QVersionNumber serverVersion() const { return m_serverVersion; }

    pw_loop *loop() const { return m_pwMainLoop; }
    pw_core *operator*() const { return m_pwCore; };
    static QSharedPointer<PipewireCore> fetch(int fd);

Q_SIGNALS:
    void pipewireFailed(const QString &message);

private:
    pw_core *m_pwCore = nullptr;
    pw_context *m_pwContext = nullptr;
    pw_loop *m_pwMainLoop = nullptr;
    spa_hook m_coreListener;
    QString m_error;
    QVersionNumber m_serverVersion;
    static pw_core_events s_pwCoreEvents;
};

#endif // PIPEWIRECORE_H
