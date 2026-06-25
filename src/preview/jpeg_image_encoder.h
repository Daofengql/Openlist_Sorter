#ifndef OPENLIST_SORTER_PREVIEW_JPEG_IMAGE_ENCODER_H
#define OPENLIST_SORTER_PREVIEW_JPEG_IMAGE_ENCODER_H

#include <QByteArray>
#include <QImage>
#include <QString>

class JpegImageEncoder {
 public:
  static bool encode(const QImage& image,
                     int quality,
                     QByteArray* jpegData,
                     QString* errorMessage);
};

#endif
