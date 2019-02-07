#ifndef QTDATASYNCIOS_GLOBAL_H
#define QTDATASYNCIOS_GLOBAL_H

#include <QtCore/qglobal.h>

#ifndef QT_STATIC
#  if defined(QT_BUILD_DATASYNCIOS_LIB)
#    define Q_DATASYNCIOS_EXPORT Q_DECL_EXPORT
#  else
#    define Q_DATASYNCIOS_EXPORT Q_DECL_IMPORT
#  endif
#else
#  define Q_DATASYNCIOS_EXPORT
#endif

#endif // QTDATASYNCIOS_GLOBAL_H
