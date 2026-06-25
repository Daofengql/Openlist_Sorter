#ifndef OPENLIST_SORTER_PREVIEW_WIC_IMAGE_DECODER_H
#define OPENLIST_SORTER_PREVIEW_WIC_IMAGE_DECODER_H

#include <QByteArray>
#include <QString>

#include "preview/image_decoder.h"

class WicImageDecoder {
 public:
  static ImageDecodeResult decodeToImage(const QByteArray& data,
                                         const QString& sourceName);
};

#endif
