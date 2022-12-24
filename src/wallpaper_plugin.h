#ifndef WALLPAPER_PLUGIN_H
#define WALLPAPER_PLUGIN_H

#include <QQmlExtensionPlugin>

class Wallpaper_Plugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)

public:
    void registerTypes(const char *uri) override;
};

#endif // WALLPAPER_PLUGIN_H
