#include "preview/preview_cache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSaveFile>

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

bool PreviewCache::loadImage(const RemoteEntry& entry,
                             QImage* image,
                             QString* cachePath) {
  if (!image) {
    return false;
  }

  const QString path = cachePathForEntry(entry);
  if (cachePath) {
    *cachePath = path;
  }
  if (path.isEmpty()) {
    return false;
  }

  QImage loaded;
  if (!loaded.load(path)) {
    return false;
  }
  *image = loaded;
  return true;
}

bool PreviewCache::storeImage(const RemoteEntry& entry,
                              const QImage& image,
                              QString* cachePath) {
  if (image.isNull()) {
    return false;
  }

  const QString path = cachePathForEntry(entry);
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
  if (!image.save(&file, "PNG")) {
    file.cancelWriting();
    return false;
  }
  return file.commit();
}

QString PreviewCache::cachePathForEntry(const RemoteEntry& entry) {
  const QString key = cacheKeyForEntry(entry);
  if (key.isEmpty()) {
    return {};
  }
  return QDir(cacheDirectoryPath()).filePath(key + ".png");
}

QString PreviewCache::cacheKeyForEntry(const RemoteEntry& entry) {
  if (entry.path.isEmpty()) {
    return {};
  }

  QByteArray material;
  material.append(entry.path.toUtf8());
  material.append('\n');
  material.append(QByteArray::number(entry.size));
  material.append('\n');
  material.append(entry.modified.toUTC().toString(Qt::ISODateWithMs).toUtf8());
  material.append('\n');
  material.append(entry.name.toUtf8());

  const QByteArray digest =
      QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex();
  return QString::fromLatin1(digest);
}
