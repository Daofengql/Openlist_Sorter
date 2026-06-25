#include "preview/image_decoder.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include "preview/heif_image_decoder.h"
#include "preview/webp_image_encoder.h"
#include "preview/wic_image_decoder.h"

namespace {

QString cleanSuffix(const QString& sourceName) {
  QString suffix = QFileInfo(sourceName).suffix().toLower();
  if (suffix.isEmpty()) {
    suffix = "bin";
  }
  for (const QChar character : suffix) {
    if (!character.isLetterOrNumber()) {
      return "bin";
    }
  }
  return suffix;
}

bool dataStartsWith(const QByteArray& data, const QByteArray& prefix) {
  return data.size() >= prefix.size() && data.left(prefix.size()) == prefix;
}

bool brandListContains(const QByteArray& brands, const QByteArray& brand) {
  return brands.contains(brand);
}

QString detectedSuffixForData(const QByteArray& data,
                              const QString& sourceName) {
  if (data.size() >= 12 && data.mid(4, 4) == "ftyp") {
    const QByteArray brands = data.mid(8, qMin(data.size() - 8, 96));
    if (brandListContains(brands, "avif") || brandListContains(brands, "avis")) {
      return "avif";
    }
    if (brandListContains(brands, "heic") || brandListContains(brands, "heix") ||
        brandListContains(brands, "hevc") || brandListContains(brands, "hevx") ||
        brandListContains(brands, "heis") || brandListContains(brands, "hevm") ||
        brandListContains(brands, "mif1") || brandListContains(brands, "msf1")) {
      return "heic";
    }
    return "heif";
  }

  if (dataStartsWith(data, QByteArray::fromHex("ffd8ff"))) {
    return "jpg";
  }
  if (dataStartsWith(data, QByteArray::fromHex("89504e470d0a1a0a"))) {
    return "png";
  }
  if (dataStartsWith(data, "GIF87a") || dataStartsWith(data, "GIF89a")) {
    return "gif";
  }
  if (dataStartsWith(data, "BM")) {
    return "bmp";
  }
  if (dataStartsWith(data, QByteArray::fromHex("00000100"))) {
    return "ico";
  }
  if (data.size() >= 12 && data.left(4) == "RIFF" && data.mid(8, 4) == "WEBP") {
    return "webp";
  }
  if (dataStartsWith(data, QByteArray("II*\0", 4)) ||
      dataStartsWith(data, QByteArray("MM\0*", 4))) {
    return "tif";
  }

  const QByteArray trimmed = data.left(512).trimmed();
  if (trimmed.startsWith("<svg") || trimmed.startsWith("<?xml")) {
    return "svg";
  }

  return cleanSuffix(sourceName);
}

QString imageSignatureSummary(const QByteArray& data,
                              const QString& sourceName) {
  QStringList parts;
  parts.push_back("文件名后缀=" + cleanSuffix(sourceName));
  parts.push_back("内容识别=" + detectedSuffixForData(data, sourceName));
  if (data.size() >= 12 && data.mid(4, 4) == "ftyp") {
    parts.push_back("ftyp=" + QString::fromLatin1(data.mid(8, 4)));
    parts.push_back("brands=" +
                    QString::fromLatin1(data.mid(8, qMin(data.size() - 8, 48))));
  }
  parts.push_back("header=" + QString::fromLatin1(data.left(16).toHex(' ')));
  return parts.join(", ");
}

bool shouldCompareContainerDecoders(const QByteArray& data,
                                    const QString& sourceName) {
  const QString suffix = detectedSuffixForData(data, sourceName);
  return suffix == "heic" || suffix == "heif" || suffix == "avif";
}

bool shouldDecodeWithWebp(const QByteArray& data, const QString& sourceName) {
  return detectedSuffixForData(data, sourceName) == "webp";
}

bool shouldDecodeWithQt(const QByteArray& data, const QString& sourceName) {
  const QString suffix = detectedSuffixForData(data, sourceName);
  return suffix == "svg" || suffix == "svgz";
}

bool shouldDecodeWithWic(const QByteArray& data, const QString& sourceName) {
  const QString suffix = detectedSuffixForData(data, sourceName);
  return suffix == "jpg" || suffix == "jpeg" || suffix == "png" ||
         suffix == "gif" || suffix == "bmp" || suffix == "tif" ||
         suffix == "tiff" || suffix == "ico";
}

QString safeDiagnosticName(const QString& sourceName, const QString& suffix) {
  QString baseName = QFileInfo(sourceName).completeBaseName();
  if (baseName.isEmpty()) {
    baseName = "image";
  }
  QString sanitized;
  for (const QChar character : baseName) {
    sanitized.append(character.isLetterOrNumber() ? character : QChar('_'));
  }
  if (sanitized.size() > 48) {
    sanitized = sanitized.left(48);
  }
  const QString timestamp =
      QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
  return timestamp + "-" + sanitized + "." + suffix;
}

QString writeDiagnosticCopy(const QByteArray& data, const QString& sourceName) {
  const QString suffix = detectedSuffixForData(data, sourceName);
  const QString directoryPath =
      QDir(QDir::tempPath()).filePath("openlist-sorter-failed-previews");
  QDir directory;
  if (!directory.mkpath(directoryPath)) {
    return {};
  }

  const QString path =
      QDir(directoryPath).filePath(safeDiagnosticName(sourceName, suffix));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return {};
  }
  file.write(data);
  return QDir::toNativeSeparators(path);
}

ImageDecodeResult decodeWithQt(const QByteArray& data,
                               const QString& sourceName) {
  QBuffer buffer;
  buffer.setData(data);
  buffer.open(QIODevice::ReadOnly);

  QImageReader reader(&buffer);
  reader.setDecideFormatFromContent(true);
  reader.setAutoTransform(true);

  QImage image = reader.read();
  if (image.isNull()) {
    return {false, {}, reader.errorString(), "Qt ImageReader"};
  }

  return {true, image, "Qt ImageReader", "Qt ImageReader"};
}

ImageDecodeResult decodeWithWebp(const QByteArray& data) {
  QImage image;
  QString errorMessage;
  if (!WebpImageEncoder::decode(data, &image, &errorMessage)) {
    return {false, {}, "libwebp 解码失败: " + errorMessage, "libwebp"};
  }

  return {true, image, "libwebp 直接解码", "libwebp"};
}

}  // namespace

ImageDecodeResult ImageDecoder::decodeToImage(const QByteArray& data,
                                              const QString& sourceName) {
  const bool heifLike = shouldCompareContainerDecoders(data, sourceName);
  const bool webpLike = shouldDecodeWithWebp(data, sourceName);

  if (heifLike) {
    ImageDecodeResult heifResult =
        HeifImageDecoder::decodeToImage(data, sourceName, "未使用 Qt HEIF 插件",
                                        "未使用通用转码中间层");
    if (heifResult.ok) {
      return heifResult;
    }

    const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
    heifResult.message += "。文件识别: " +
                          imageSignatureSummary(data, sourceName);
    if (!diagnosticPath.isEmpty()) {
      heifResult.message += "。已保存诊断副本: " + diagnosticPath;
    }
    return heifResult;
  }

  if (webpLike) {
    ImageDecodeResult webpResult = decodeWithWebp(data);
    if (webpResult.ok) {
      return webpResult;
    }

    const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
    webpResult.message += "。文件识别: " +
                          imageSignatureSummary(data, sourceName);
    if (!diagnosticPath.isEmpty()) {
      webpResult.message += "。已保存诊断副本: " + diagnosticPath;
    }
    return webpResult;
  }

#ifdef Q_OS_WIN
  if (shouldDecodeWithWic(data, sourceName)) {
    ImageDecodeResult wicResult = WicImageDecoder::decodeToImage(data, sourceName);
    if (wicResult.ok) {
      return wicResult;
    }

    const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
    wicResult.message += "。文件识别: " +
                         imageSignatureSummary(data, sourceName);
    if (!diagnosticPath.isEmpty()) {
      wicResult.message += "。已保存诊断副本: " + diagnosticPath;
    }
    return wicResult;
  }
#endif

  if (shouldDecodeWithQt(data, sourceName)) {
    ImageDecodeResult qtResult = decodeWithQt(data, sourceName);
    if (qtResult.ok) {
      return qtResult;
    }

    const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
    qtResult.message += "。文件识别: " + imageSignatureSummary(data, sourceName);
    if (!diagnosticPath.isEmpty()) {
      qtResult.message += "。已保存诊断副本: " + diagnosticPath;
    }
    return qtResult;
  }

#ifndef Q_OS_WIN
  ImageDecodeResult qtResult = decodeWithQt(data, sourceName);
  if (qtResult.ok) {
    return qtResult;
  }
#else
  ImageDecodeResult qtResult =
      {false, {}, "该格式还没有接入独立底层解码库，已避免使用重复的通用转码链路。",
       "direct codec router"};
#endif
  const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
  qtResult.message += "。文件识别: " + imageSignatureSummary(data, sourceName);
  if (!diagnosticPath.isEmpty()) {
    qtResult.message += "。已保存诊断副本: " + diagnosticPath;
  }
  return qtResult;
}

QString ImageDecoder::supportedFormatsSummary() {
#ifdef Q_OS_WIN
  return "WIC: bmp, gif, ico, jpeg, jpg, png, tif, tiff; libwebp: webp; "
         "libheif: avif, heic, heif; Qt: svg, svgz";
#else
  return "libwebp: webp; libheif: avif, heic, heif; Qt: svg and remaining "
         "platform-supported formats";
#endif
}
