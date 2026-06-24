#ifndef OPENLIST_SORTER_PREVIEW_IMAGE_DECODER_H
#define OPENLIST_SORTER_PREVIEW_IMAGE_DECODER_H

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>

struct ImageDecodeResult {
  bool ok{};
  QImage image;
  QString message;
  QString decoderName;
};

class ImageDecoder {
 public:
  static ImageDecodeResult decodeToImage(const QByteArray& data,
                                         const QString& sourceName);
  static QString supportedFormatsSummary();
};

#endif
