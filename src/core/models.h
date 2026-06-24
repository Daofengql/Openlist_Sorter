#ifndef OPENLIST_SORTER_MODELS_H
#define OPENLIST_SORTER_MODELS_H

#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QVector>

struct RemoteEntry {
  QString name;
  QString path;
  qint64 size{};
  bool isDir{};
  int type{};
  QDateTime modified;
  QDateTime created;
  QString thumb;
  QString rawUrl;
};

struct TagMapping {
  QString name;
  QString targetPath;
};

struct FileItem {
  RemoteEntry entry;
  QStringList selectedTags;
  bool submitted{};
  bool failed{};
  QString lastError;
};

inline QString normalizeRemotePath(QString path) {
  path = path.trimmed().replace('\\', '/');
  if (path.isEmpty()) {
    return "/";
  }
  if (!path.startsWith('/')) {
    path.prepend('/');
  }
  while (path.contains("//")) {
    path.replace("//", "/");
  }
  while (path.size() > 1 && path.endsWith('/')) {
    path.chop(1);
  }
  return path;
}

inline QString joinRemotePath(const QString& directory, const QString& name) {
  const QString normalizedDirectory = normalizeRemotePath(directory);
  if (normalizedDirectory == "/") {
    return "/" + name;
  }
  return normalizedDirectory + "/" + name;
}

inline QString parentRemotePath(const QString& path) {
  const QString normalizedPath = normalizeRemotePath(path);
  if (normalizedPath == "/") {
    return "/";
  }
  const int slash = normalizedPath.lastIndexOf('/');
  if (slash <= 0) {
    return "/";
  }
  return normalizedPath.left(slash);
}

inline QString fileNameFromRemotePath(const QString& path) {
  const QString normalizedPath = normalizeRemotePath(path);
  const int slash = normalizedPath.lastIndexOf('/');
  if (slash < 0 || slash == normalizedPath.size() - 1) {
    return normalizedPath;
  }
  return normalizedPath.mid(slash + 1);
}

inline QString formatBytes(qint64 bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(bytes);
  int unitIndex = 0;
  while (value >= 1024.0 && unitIndex < 4) {
    value /= 1024.0;
    ++unitIndex;
  }
  if (unitIndex == 0) {
    return QString::number(bytes) + " B";
  }
  return QString::number(value, 'f', value >= 10.0 ? 1 : 2) + " " +
         units[unitIndex];
}

inline QString lowerSuffix(const QString& name) {
  const QString suffix = QFileInfo(name).suffix().toLower();
  return suffix;
}

inline bool isImageFileName(const QString& name) {
  const QString suffix = lowerSuffix(name);
  return suffix == "jpg" || suffix == "jpeg" || suffix == "png" ||
         suffix == "gif" || suffix == "bmp" || suffix == "webp" ||
         suffix == "tif" || suffix == "tiff" || suffix == "heic" ||
         suffix == "heif" || suffix == "avif" || suffix == "jxl" ||
         suffix == "ico" || suffix == "svg" || suffix == "raw" ||
         suffix == "cr2" || suffix == "cr3" || suffix == "nef" ||
         suffix == "arw" || suffix == "dng" || suffix == "orf" ||
         suffix == "rw2" || suffix == "raf" || suffix == "pef";
}

inline bool isVideoFileName(const QString& name) {
  const QString suffix = lowerSuffix(name);
  return suffix == "mp4" || suffix == "mkv" || suffix == "mov" ||
         suffix == "avi" || suffix == "webm" || suffix == "flv" ||
         suffix == "wmv" || suffix == "m4v" || suffix == "ts";
}

inline QString statusTextForFile(const FileItem& item) {
  if (item.submitted) {
    return "已提交";
  }
  if (item.failed) {
    return "失败";
  }
  if (!item.selectedTags.isEmpty()) {
    return "待保存";
  }
  return "未处理";
}

#endif
