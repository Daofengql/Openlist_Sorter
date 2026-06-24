#ifndef OPENLIST_SORTER_PREVIEW_CACHE_H
#define OPENLIST_SORTER_PREVIEW_CACHE_H

#include <QImage>
#include <QString>

#include "core/models.h"

struct PreviewCacheStats {
  int fileCount{};
  qint64 totalBytes{};
};

class PreviewCache {
 public:
  static bool loadImage(const RemoteEntry& entry,
                        QImage* image,
                        QString* cachePath = nullptr);
  static bool storeImage(const RemoteEntry& entry,
                         const QImage& image,
                         QString* cachePath = nullptr);
  static QString cacheDirectoryPath();
  static PreviewCacheStats stats();
  static bool clear(QString* errorMessage = nullptr);

 private:
  static QString cachePathForEntry(const RemoteEntry& entry);
  static QString cacheKeyForEntry(const RemoteEntry& entry);
};

#endif
