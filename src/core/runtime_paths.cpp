#include "core/runtime_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QSet>
#include <QStringList>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

QString canonicalDirectoryPath(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists() || !info.isDir()) {
    return {};
  }
  return QDir::cleanPath(info.canonicalFilePath().isEmpty()
                             ? info.absoluteFilePath()
                             : info.canonicalFilePath());
}

void appendCandidate(QStringList* candidates, const QString& path) {
  const QString cleanPath = QDir::cleanPath(path);
  if (!cleanPath.isEmpty() && !candidates->contains(cleanPath)) {
    candidates->push_back(cleanPath);
  }
}

}  // namespace

QString RuntimePaths::runtimeDirectory(const QString& name) {
  QStringList candidates;
  const QString appDir = QCoreApplication::applicationDirPath();
  const QString currentDir = QDir::currentPath();

  appendCandidate(&candidates, QDir(appDir).filePath("runtime/" + name));
  appendCandidate(&candidates, QDir(appDir).filePath(name));
  appendCandidate(&candidates,
                  QDir(currentDir).filePath("third_party/runtime/" + name));
  appendCandidate(&candidates, QDir(currentDir).filePath("runtime/" + name));
  appendCandidate(&candidates,
                  QDir(appDir).filePath("../third_party/runtime/" + name));
  appendCandidate(&candidates,
                  QDir(appDir).filePath("../../third_party/runtime/" + name));

  for (const QString& candidate : candidates) {
    const QString canonical = canonicalDirectoryPath(candidate);
    if (!canonical.isEmpty()) {
      return canonical;
    }
  }
  return {};
}

void RuntimePaths::addDllSearchPath(const QString& directoryPath) {
  const QString canonical = canonicalDirectoryPath(directoryPath);
  if (canonical.isEmpty()) {
    return;
  }

  static QMutex mutex;
  static QSet<QString> addedPaths;

  QMutexLocker locker(&mutex);
  if (addedPaths.contains(canonical)) {
    return;
  }
  addedPaths.insert(canonical);

  const QByteArray nativePath =
      QDir::toNativeSeparators(canonical).toLocal8Bit();
  const QByteArray separator =
      QString(QDir::listSeparator()).toLocal8Bit();
  const QByteArray currentPath = qgetenv("PATH");
  qputenv("PATH", nativePath + separator + currentPath);

#ifdef Q_OS_WIN
  SetDllDirectoryW(reinterpret_cast<const wchar_t*>(
      QDir::toNativeSeparators(canonical).utf16()));
#endif
}
