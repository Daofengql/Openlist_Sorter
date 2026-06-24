#ifndef OPENLIST_SORTER_PREVIEW_IMAGE_CONVERTER_H
#define OPENLIST_SORTER_PREVIEW_IMAGE_CONVERTER_H

#include <QByteArray>
#include <QString>

struct ImageConversionResult {
  bool ok{};
  QByteArray data;
  QString outputName;
  QString sha256;
  QString md5;
  QString hashSource;
  QString message;
};

class ImageConverter {
 public:
  static ImageConversionResult convertToWebp(const QByteArray& data,
                                             const QString& sourceName,
                                             const QString& hashSource);
};

#endif
