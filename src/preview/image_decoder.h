#ifndef OPENLIST_SORTER_PREVIEW_IMAGE_DECODER_H
#define OPENLIST_SORTER_PREVIEW_IMAGE_DECODER_H

#include <QByteArray>
#include <QPixmap>
#include <QString>
#include <QStringList>

struct ImageDecodeResult {
  bool ok{};
  QPixmap pixmap;
  QString message;
  QString decoderName;
};

class ImageDecoder {
 public:
  static ImageDecodeResult decodeToPixmap(const QByteArray& data,
                                          const QString& sourceName);
  static QString supportedFormatsSummary();
};

#endif

