#ifndef WALLPAPERGLOBAL_H
#define WALLPAPERGLOBAL_H

#include <QtGlobal>

#if defined(WSM_WALLPAPER)
#define WSM_WALLPAPER_EXPORT Q_DECL_EXPORT
#else
#define WSM_WALLPAPER_EXPORT Q_DECL_IMPORT
#endif

#endif // WALLPAPERGLOBAL_H
