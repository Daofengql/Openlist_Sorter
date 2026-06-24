#ifndef OPENLIST_SORTER_PREVIEW_MAGICK_WAND_BRIDGE_H
#define OPENLIST_SORTER_PREVIEW_MAGICK_WAND_BRIDGE_H

#include <QByteArray>
#include <QString>

class MagickWandBridge {
 public:
  static bool decodeToPng(const QByteArray& data,
                          QByteArray* pngData,
                          QString* errorMessage);
  static bool convertToWebp(const QByteArray& data,
                            QByteArray* webpData,
                            QString* errorMessage);
};

#endif
