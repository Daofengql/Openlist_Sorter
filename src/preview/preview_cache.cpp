#include "preview/preview_cache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "preview/image_decoder.h"
#include "preview/jpeg_image_encoder.h"
#include "preview/webp_image_encoder.h"

namespace {

QString extensionForVariant(const QString& variant) {
  if (variant == "raw") {
    return ".bin";
  }
  if (variant == "thumb") {
    return ".webp";
  }
  return ".jpg";
}

QString cacheVersionForVariant(const QString& variant) {
  if (variant == "preview") {
    return "preview-heif-jpeg-v1";
  }
  if (variant == "thumb") {
    return "thumb-webp-v1";
  }
  return "v1";
}

bool loadBytesFromPath(const QString& path, QByteArray* data) {
  if (!data || path.isEmpty()) {
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  *data = file.readAll();
  return !data->isEmpty();
}

bool storeBytesToPath(const QString& path, const QByteArray& data) {
  if (path.isEmpty() || data.isEmpty()) {
    return false;
  }

  QDir directory;
  if (!directory.mkpath(PreviewCache::cacheDirectoryPath())) {
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  file.write(data);
  return file.commit();
}

QByteArray encodeThumbnailImage(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  QByteArray data;
  QString errorMessage;
  if (!WebpImageEncoder::encode(image, 90, &data, &errorMessage)) {
    return {};
  }
  return data;
}

}  // namespace

QString PreviewCache::cacheDirectoryPath() {
  return QDir(QCoreApplication::applicationDirPath()).filePath("cache/previews");
}

PreviewCacheStats PreviewCache::stats() {
  PreviewCacheStats result;
  const QDir directory(cacheDirectoryPath());
  if (!directory.exists()) {
    return result;
  }

  QDirIterator iterator(directory.absolutePath(),
                        QDir::Files | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const QFileInfo fileInfo = iterator.fileInfo();
    ++result.fileCount;
    result.totalBytes += fileInfo.size();
  }
  return result;
}

bool PreviewCache::clear(QString* errorMessage) {
  const QString path = cacheDirectoryPath();
  QDir directory(path);
  if (!directory.exists()) {
    return true;
  }

  const QFileInfo pathInfo(path);
  if (!pathInfo.isDir()) {
    if (errorMessage) {
      *errorMessage = "缓存路径存在，但不是目录: " + path;
    }
    return false;
  }

  if (!directory.removeRecursively()) {
    if (errorMessage) {
      *errorMessage = "无法删除缓存目录，可能有文件正在被占用: " + path;
    }
    return false;
  }
  return true;
}

QByteArray PreviewCache::encodePreviewImage(const QImage& image) {
  if (image.isNull()) {
    return {};
  }

  QByteArray data;
  QString errorMessage;
  if (!JpegImageEncoder::encode(image, 100, &data, &errorMessage)) {
    return {};
  }
  return data;
}

bool PreviewCache::loadImage(const RemoteEntry& entry,
                             QImage* image,
                             QString* cachePath) {
  if (!image) {
    return false;
  }

  const QString path = cachePathForEntry(entry, "preview");
  if (cachePath) {
    *cachePath = path;
  }
  if (path.isEmpty()) {
    return false;
  }

  QByteArray data;
  if (!loadBytesFromPath(path, &data)) {
    return false;
  }

  ImageDecodeResult decoded =
      ImageDecoder::decodeToImage(data, QFileInfo(path).fileName());
  if (!decoded.ok || decoded.image.isNull()) {
    return false;
  }
  *image = decoded.image;
  return true;
}

bool PreviewCache::storeImage(const RemoteEntry& entry,
                              const QImage& image,
                              QString* cachePath) {
  if (image.isNull()) {
    return false;
  }

  const QString path = cachePathForEntry(entry, "preview");
  if (cachePath) {
    *cachePath = path;
  }
  if (path.isEmpty()) {
    return false;
  }

  QDir directory;
  if (!directory.mkpath(cacheDirectoryPath())) {
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  const QByteArray encoded = encodePreviewImage(image);
  if (encoded.isEmpty()) {
    file.cancelWriting();
    return false;
  }
  file.write(encoded);
  return file.commit();
}

bool PreviewCache::loadThumbnail(const RemoteEntry& entry,
                                 QImage* image,
                                 QString* cachePath) {
  if (entry.thumb.isEmpty()) {
    return false;
  }
  const QString path = cachePathForEntry(entry, "thumb");
  if (cachePath) {
    *cachePath = path;
  }
  if (!image || path.isEmpty()) {
    return false;
  }

  QByteArray data;
  if (!loadBytesFromPath(path, &data)) {
    return false;
  }

  QImage loaded;
  QString errorMessage;
  if (!WebpImageEncoder::decode(data, &loaded, &errorMessage)) {
    return false;
  }
  *image = loaded;
  return true;
}

bool PreviewCache::storeThumbnail(const RemoteEntry& entry,
                                  const QImage& image,
                                  QString* cachePath) {
  if (entry.thumb.isEmpty() || image.isNull()) {
    return false;
  }

  const QString path = cachePathForEntry(entry, "thumb");
  if (cachePath) {
    *cachePath = path;
  }
  if (path.isEmpty()) {
    return false;
  }

  QDir directory;
  if (!directory.mkpath(cacheDirectoryPath())) {
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  const QByteArray encoded = encodeThumbnailImage(image);
  if (encoded.isEmpty()) {
    file.cancelWriting();
    return false;
  }
  file.write(encoded);
  return file.commit();
}

bool PreviewCache::loadRawData(const RemoteEntry& entry,
                               QByteArray* data,
                               QString* cachePath) {
  const QString path = cachePathForEntry(entry, "raw");
  if (cachePath) {
    *cachePath = path;
  }
  return loadBytesFromPath(path, data);
}

bool PreviewCache::storeRawData(const RemoteEntry& entry,
                                const QByteArray& data,
                                QString* cachePath) {
  const QString path = cachePathForEntry(entry, "raw");
  if (cachePath) {
    *cachePath = path;
  }
  return storeBytesToPath(path, data);
}

bool PreviewCache::loadImageData(const RemoteEntry& entry,
                                 QByteArray* data,
                                 QString* cachePath) {
  const QString path = cachePathForEntry(entry, "preview");
  if (cachePath) {
    *cachePath = path;
  }
  return loadBytesFromPath(path, data);
}

QString PreviewCache::cachePathForEntry(const RemoteEntry& entry,
                                        const QString& variant) {
  const QString key = cacheKeyForEntry(entry, variant);
  if (key.isEmpty()) {
    return {};
  }
  return QDir(cacheDirectoryPath()).filePath(key + extensionForVariant(variant));
}

QString PreviewCache::cacheKeyForEntry(const RemoteEntry& entry,
                                       const QString& variant) {
  if (entry.path.isEmpty()) {
    return {};
  }

  QByteArray material;
  material.append(cacheVersionForVariant(variant).toUtf8());
  material.append('\n');
  material.append(variant.toUtf8());
  material.append('\n');
  material.append(entry.path.toUtf8());
  material.append('\n');
  material.append(QByteArray::number(entry.size));
  material.append('\n');
  material.append(entry.modified.toUTC().toString(Qt::ISODateWithMs).toUtf8());
  material.append('\n');
  material.append(entry.name.toUtf8());
  if (variant == "thumb") {
    material.append('\n');
    material.append(entry.thumb.toUtf8());
  }

  const QByteArray digest =
      QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
  return QString::fromLatin1(digest);
}
