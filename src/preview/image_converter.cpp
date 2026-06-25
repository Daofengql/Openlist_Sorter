#include "preview/image_converter.h"

#include <QCryptographicHash>

#include "preview/magick_wand_bridge.h"

namespace {

QString hashHex(const QByteArray& data, QCryptographicHash::Algorithm algorithm) {
  return QString::fromLatin1(QCryptographicHash::hash(data, algorithm).toHex());
}

}  // namespace

ImageConversionResult ImageConverter::convertToWebp(
    const QByteArray& data,
    const QString& sourceName,
    const QString& hashSource) {
  Q_UNUSED(sourceName);

  ImageConversionResult result;
  result.hashSource = hashSource;
  if (data.isEmpty()) {
    result.message = "图片数据为空，无法转换。";
    return result;
  }

  result.sha256 = hashHex(data, QCryptographicHash::Sha256);
  result.md5 = hashHex(data, QCryptographicHash::Md5);
  result.outputName = result.sha256 + ".webp";

  QString errorMessage;
  if (!MagickWandBridge::convertToWebp(data, &result.data, &errorMessage)) {
    result.message = "MagickWand 运行库转 WebP 失败: " + errorMessage;
    return result;
  }
  result.ok = !result.data.isEmpty();
  if (!result.ok) {
    result.message = "WebP 转换结果为空。";
  } else {
    result.message = "converted";
  }
  return result;
}
