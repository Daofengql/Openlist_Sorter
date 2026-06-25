#ifndef OPENLIST_SORTER_PREVIEW_WEBP_IMAGE_ENCODER_H
#define OPENLIST_SORTER_PREVIEW_WEBP_IMAGE_ENCODER_H

#include <QByteArray>
#include <QImage>
#include <QString>

class WebpImageEncoder {
 public:
  static bool decode(const QByteArray& data,
                     QImage* image,
                     QString* errorMessage);
  static bool encode(const QImage& image,
                     int quality,
                     QByteArray* webpData,
                     QString* errorMessage);
};

#endif
