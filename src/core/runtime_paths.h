#ifndef OPENLIST_SORTER_CORE_RUNTIME_PATHS_H
#define OPENLIST_SORTER_CORE_RUNTIME_PATHS_H

#include <QString>

class RuntimePaths {
 public:
  static QString runtimeDirectory(const QString& name);
  static void addDllSearchPath(const QString& directoryPath);
};

#endif
