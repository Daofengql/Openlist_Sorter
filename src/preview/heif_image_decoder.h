#ifndef OPENLIST_SORTER_PREVIEW_HEIF_IMAGE_DECODER_H
#define OPENLIST_SORTER_PREVIEW_HEIF_IMAGE_DECODER_H

#include <QByteArray>
#include <QString>

#include "preview/image_decoder.h"

class HeifImageDecoder {
 public:
  static ImageDecodeResult decodeToImage(const QByteArray& data,
                                         const QString& sourceName,
                                         const QString& qtError,
                                         const QString& imageMagickError);
};

#endif
