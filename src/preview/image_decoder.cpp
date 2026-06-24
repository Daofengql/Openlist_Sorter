#include "preview/image_decoder.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>

#include "preview/ffmpeg_image_decoder.h"
#include "preview/magick_wand_bridge.h"

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

qint64 imageArea(const QImage& image) {
  if (image.isNull()) {
    return 0;
  }
  return static_cast<qint64>(image.width()) * image.height();
}

void keepLargerImage(ImageDecodeResult* best,
                     const ImageDecodeResult& candidate) {
  if (!candidate.ok) {
    return;
  }
  if (!best->ok || imageArea(candidate.image) > imageArea(best->image)) {
    *best = candidate;
  }
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

ImageDecodeResult decodeWithImageMagick(const QByteArray& data,
                                        const QString& sourceName,
                                        const QString& qtError) {
  Q_UNUSED(sourceName);

  QByteArray pngData;
  QString errorMessage;
  if (!MagickWandBridge::decodeToPng(data, &pngData, &errorMessage)) {
    return {false,
            {},
            "MagickWand DLL 转码失败: " + errorMessage +
                "。Qt 错误: " + qtError,
            "MagickWand DLL"};
  }

  QImage image = QImage::fromData(pngData, "PNG");
  if (image.isNull()) {
    return {false, {}, "MagickWand 已输出 PNG，但 Qt 无法读取结果。",
            "MagickWand DLL"};
  }

  return {true, image, "MagickWand DLL 内存转码", "MagickWand DLL"};
}

}  // namespace

ImageDecodeResult ImageDecoder::decodeToImage(const QByteArray& data,
                                              const QString& sourceName) {
  const bool compareContainerDecoders =
      shouldCompareContainerDecoders(data, sourceName);

  ImageDecodeResult qtResult = decodeWithQt(data, sourceName);
  if (qtResult.ok && !compareContainerDecoders) {
    return qtResult;
  }

  ImageDecodeResult bestResult;
  keepLargerImage(&bestResult, qtResult);

  ImageDecodeResult imageMagickResult =
      decodeWithImageMagick(data, sourceName, qtResult.message);
  if (imageMagickResult.ok && !compareContainerDecoders) {
    return imageMagickResult;
  }
  keepLargerImage(&bestResult, imageMagickResult);

  ImageDecodeResult ffmpegResult =
      FfmpegImageDecoder::decodeToImage(data, sourceName, qtResult.message,
                                        imageMagickResult.message);
  keepLargerImage(&bestResult, ffmpegResult);
  if (bestResult.ok) {
    return bestResult;
  }

  if (!ffmpegResult.ok) {
    const QString diagnosticPath = writeDiagnosticCopy(data, sourceName);
    ffmpegResult.message += "。文件识别: " +
                            imageSignatureSummary(data, sourceName);
    if (!diagnosticPath.isEmpty()) {
      ffmpegResult.message += "。已保存诊断副本: " + diagnosticPath;
    }
  }
  return ffmpegResult;
}

QString ImageDecoder::supportedFormatsSummary() {
  QStringList formats;
  const QList<QByteArray> supported = QImageReader::supportedImageFormats();
  for (const QByteArray& format : supported) {
    formats.push_back(QString::fromLatin1(format).toLower());
  }
  formats.removeDuplicates();
  formats.sort(Qt::CaseInsensitive);
  return formats.join(", ");
}
