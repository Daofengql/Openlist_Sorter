#include "preview/image_decoder.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

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

ImageDecodeResult decodeWithQt(const QByteArray& data,
                               const QString& sourceName) {
  QBuffer buffer;
  buffer.setData(data);
  buffer.open(QIODevice::ReadOnly);

  QImageReader reader(&buffer, cleanSuffix(sourceName).toLatin1());
  reader.setAutoTransform(true);

  QImage image = reader.read();
  if (image.isNull()) {
    return {false, {}, reader.errorString(), "Qt ImageReader"};
  }

  return {true, QPixmap::fromImage(image), "Qt ImageReader", "Qt ImageReader"};
}

ImageDecodeResult decodeWithImageMagick(const QByteArray& data,
                                        const QString& sourceName,
                                        const QString& qtError) {
  const QString magickPath = QStandardPaths::findExecutable("magick");
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

  const QString suffix = cleanSuffix(sourceName);
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

  QPixmap pixmap;
  if (!pixmap.load(outputPath)) {
    return {false, {}, "ImageMagick 已输出 PNG，但 Qt 无法读取结果。", "ImageMagick"};
  }

  return {true, pixmap, "ImageMagick 临时转码", "ImageMagick"};
}

}  // namespace

ImageDecodeResult ImageDecoder::decodeToPixmap(const QByteArray& data,
                                               const QString& sourceName) {
  ImageDecodeResult qtResult = decodeWithQt(data, sourceName);
  if (qtResult.ok) {
    return qtResult;
  }
  return decodeWithImageMagick(data, sourceName, qtResult.message);
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

