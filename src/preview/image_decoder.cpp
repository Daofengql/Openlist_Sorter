#include "preview/image_decoder.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace {

QString findExecutableWithFallback(const QString& executableName,
                                   const QStringList& fallbackPaths) {
  const QString appDirPath =
      QDir(QCoreApplication::applicationDirPath()).filePath(executableName);
  if (QFileInfo::exists(appDirPath)) {
    return appDirPath;
  }

  const QString pathExecutable = QStandardPaths::findExecutable(executableName);
  if (!pathExecutable.isEmpty()) {
    return pathExecutable;
  }

  for (const QString& fallbackPath : fallbackPaths) {
    if (QFileInfo::exists(fallbackPath)) {
      return fallbackPath;
    }
  }
  return {};
}

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
  const QString magickPath = findExecutableWithFallback(
      "magick.exe", {"D:/env/ImageMagick-7.1.1-Q16-HDRI/magick.exe",
                     "D:/env/ImageMagick-7.1.1-Q16-HDRI/magick"});
  if (magickPath.isEmpty()) {
    return {false,
            {},
            "Qt 无法解码该图片，且未找到 ImageMagick magick.exe。Qt 错误: " +
                qtError,
            "ImageMagick"};
  }

  QTemporaryDir tempDir(QDir::tempPath() + "/openlist-sorter-image-XXXXXX");
  if (!tempDir.isValid()) {
    return {false, {}, "无法创建临时目录用于图片转码。", "ImageMagick"};
  }

  const QString suffix = detectedSuffixForData(data, sourceName);
  const QString inputPath = tempDir.filePath("input." + suffix);
  const QString outputPath = tempDir.filePath("output.png");

  QFile inputFile(inputPath);
  if (!inputFile.open(QIODevice::WriteOnly)) {
    return {false, {}, "无法写入临时图片文件: " + inputFile.errorString(),
            "ImageMagick"};
  }
  inputFile.write(data);
  inputFile.close();

  QProcess process;
  process.setProgram(magickPath);
  process.setArguments({inputPath, "PNG32:" + outputPath});
  process.start();
  if (!process.waitForStarted(3000)) {
    return {false, {}, "ImageMagick 启动失败。", "ImageMagick"};
  }
  if (!process.waitForFinished(30000)) {
    process.kill();
    process.waitForFinished(1000);
    return {false, {}, "ImageMagick 转码超时。", "ImageMagick"};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());
    return {false,
            {},
            "ImageMagick 转码失败: " + stderrText.trimmed() +
                "。Qt 错误: " + qtError,
            "ImageMagick"};
  }

  QImage image;
  if (!image.load(outputPath)) {
    return {false, {}, "ImageMagick 已输出 PNG，但 Qt 无法读取结果。", "ImageMagick"};
  }

  return {true, image, "ImageMagick 临时转码", "ImageMagick"};
}

ImageDecodeResult decodeWithFfmpeg(const QByteArray& data,
                                   const QString& sourceName,
                                   const QString& qtError,
                                   const QString& imageMagickError) {
  const QString ffmpegPath = findExecutableWithFallback(
      "ffmpeg.exe", {"D:/env/ffmpeg/bin/ffmpeg.exe"});
  if (ffmpegPath.isEmpty()) {
    return {false,
            {},
            "Qt 和 ImageMagick 都无法解码该图片，且未找到 ffmpeg.exe。Qt 错误: " +
                qtError + "。ImageMagick 错误: " + imageMagickError,
            "ffmpeg"};
  }

  QTemporaryDir tempDir(QDir::tempPath() + "/openlist-sorter-image-XXXXXX");
  if (!tempDir.isValid()) {
    return {false, {}, "无法创建临时目录用于 ffmpeg 图片转码。", "ffmpeg"};
  }

  const QString suffix = detectedSuffixForData(data, sourceName);
  const QString inputPath = tempDir.filePath("input." + suffix);
  const QString outputPath = tempDir.filePath("output.png");

  QFile inputFile(inputPath);
  if (!inputFile.open(QIODevice::WriteOnly)) {
    return {false, {}, "无法写入临时图片文件: " + inputFile.errorString(),
            "ffmpeg"};
  }
  inputFile.write(data);
  inputFile.close();

  QProcess process;
  process.setProgram(ffmpegPath);
  process.setArguments({"-hide_banner",
                        "-loglevel",
                        "error",
                        "-y",
                        "-i",
                        inputPath,
                        "-frames:v",
                        "1",
                        outputPath});
  process.start();
  if (!process.waitForStarted(3000)) {
    return {false, {}, "ffmpeg 启动失败。", "ffmpeg"};
  }
  if (!process.waitForFinished(30000)) {
    process.kill();
    process.waitForFinished(1000);
    return {false, {}, "ffmpeg 转码超时。", "ffmpeg"};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    const QString stderrText =
        QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    return {false,
            {},
            "ffmpeg 转码失败: " + stderrText + "。Qt 错误: " + qtError +
                "。ImageMagick 错误: " + imageMagickError,
            "ffmpeg"};
  }

  QImage image;
  if (!image.load(outputPath)) {
    return {false, {}, "ffmpeg 已输出 PNG，但 Qt 无法读取结果。", "ffmpeg"};
  }

  return {true, image, "ffmpeg 临时转码", "ffmpeg"};
}

}  // namespace

ImageDecodeResult ImageDecoder::decodeToImage(const QByteArray& data,
                                              const QString& sourceName) {
  ImageDecodeResult qtResult = decodeWithQt(data, sourceName);
  if (qtResult.ok) {
    return qtResult;
  }
  ImageDecodeResult imageMagickResult =
      decodeWithImageMagick(data, sourceName, qtResult.message);
  if (imageMagickResult.ok) {
    return imageMagickResult;
  }
  ImageDecodeResult ffmpegResult =
      decodeWithFfmpeg(data, sourceName, qtResult.message,
                       imageMagickResult.message);
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
