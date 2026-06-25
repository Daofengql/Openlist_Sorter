#include "preview/magick_wand_bridge.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include "core/runtime_paths.h"

namespace {

using MagickBooleanType = int;
using ExceptionType = int;
using MagickWand = void;

void appendUnique(QStringList* values, const QString& value) {
  if (!value.isEmpty() && !values->contains(value)) {
    values->push_back(value);
  }
}

void appendLibraryFiles(QStringList* candidates,
                        const QString& directoryPath,
                        const QStringList& filters) {
  const QDir directory(directoryPath);
  if (!directory.exists()) {
    return;
  }
  const QFileInfoList entries =
      directory.entryInfoList(filters, QDir::Files, QDir::Name);
  for (const QFileInfo& entry : entries) {
    appendUnique(candidates, entry.absoluteFilePath());
  }
}

QString firstExistingDirectory(const QStringList& candidates) {
  for (const QString& candidate : candidates) {
    const QFileInfo info(candidate);
    if (info.exists() && info.isDir()) {
      return info.absoluteFilePath();
    }
  }
  return {};
}

void prepareImageMagickEnvironment(const QString& runtimeDir) {
  if (runtimeDir.isEmpty()) {
    return;
  }

  RuntimePaths::addDllSearchPath(runtimeDir);
  RuntimePaths::addDllSearchPath(QDir(runtimeDir).filePath("lib"));

  qputenv("MAGICK_HOME", QDir::toNativeSeparators(runtimeDir).toLocal8Bit());

  const QString configurePath = firstExistingDirectory({
      QDir(runtimeDir).filePath("config"),
      runtimeDir,
  });
  if (!configurePath.isEmpty()) {
    qputenv("MAGICK_CONFIGURE_PATH",
            QDir::toNativeSeparators(configurePath).toLocal8Bit());
  }

  const QString coderPath = firstExistingDirectory({
      QDir(runtimeDir).filePath("modules/coders"),
      QDir(runtimeDir).filePath("modules-Q16/coders"),
  });
  if (!coderPath.isEmpty()) {
    qputenv("MAGICK_CODER_MODULE_PATH",
            QDir::toNativeSeparators(coderPath).toLocal8Bit());
  }

  const QString filterPath = firstExistingDirectory({
      QDir(runtimeDir).filePath("modules/filters"),
      QDir(runtimeDir).filePath("modules-Q16/filters"),
  });
  if (!filterPath.isEmpty()) {
    qputenv("MAGICK_FILTER_MODULE_PATH",
            QDir::toNativeSeparators(filterPath).toLocal8Bit());
  }
}

QStringList magickWandLibraryCandidates(const QString& runtimeDir) {
  QStringList candidates;
#ifdef Q_OS_WIN
  if (!runtimeDir.isEmpty()) {
    appendUnique(&candidates, QDir(runtimeDir).filePath("CORE_RL_MagickWand_.dll"));
  }
#else
  const QStringList filters = {
      "libMagickWand-7*.so*",
      "libMagickWand-6*.so*",
      "libMagickWand*.so*",
  };
  if (!runtimeDir.isEmpty()) {
    appendLibraryFiles(&candidates, QDir(runtimeDir).filePath("lib"), filters);
    appendLibraryFiles(&candidates, runtimeDir, filters);
  }

  const QStringList systemLibraryDirs = {
      QCoreApplication::applicationDirPath(),
      "/usr/lib/x86_64-linux-gnu",
      "/lib/x86_64-linux-gnu",
      "/usr/lib64",
      "/usr/lib",
      "/lib64",
      "/lib",
  };
  for (const QString& directory : systemLibraryDirs) {
    appendLibraryFiles(&candidates, directory, filters);
  }

  for (const QString& libraryName : {
           "MagickWand-7.Q16HDRI",
           "MagickWand-7.Q16",
           "MagickWand-6.Q16HDRI",
           "MagickWand-6.Q16",
           "MagickWand",
       }) {
    appendUnique(&candidates, libraryName);
  }
#endif
  return candidates;
}

class MagickWandApi {
 public:
  ~MagickWandApi() {
    if (loaded_ && MagickWandTerminus_) {
      MagickWandTerminus_();
    }
  }

  bool ensureLoaded(QString* errorMessage) {
    QMutexLocker locker(&mutex_);
    if (loaded_) {
      return true;
    }
    if (attempted_) {
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }
    attempted_ = true;

    const QString runtimeDir = RuntimePaths::runtimeDirectory("imagemagick");
    prepareImageMagickEnvironment(runtimeDir);

    const QStringList candidates = magickWandLibraryCandidates(runtimeDir);
    QStringList loadErrors;
    for (const QString& candidate : candidates) {
      library_.setFileName(candidate);
      if (!library_.load()) {
        loadErrors.push_back(candidate + ": " + library_.errorString());
        continue;
      }

      QString symbolError;
      if (resolveAll(&symbolError)) {
        loadedLibraryName_ = candidate;
        break;
      }

      loadErrors.push_back(candidate + ": " + symbolError);
      library_.unload();
    }

    if (!library_.isLoaded()) {
      loadError_ = "加载 ImageMagick 运行库失败。";
      if (runtimeDir.isEmpty()) {
        loadError_ += " 未找到 runtime/imagemagick 或 "
                      "third_party/runtime/imagemagick。";
      }
      if (!loadErrors.isEmpty()) {
        loadError_ += " 尝试结果: " + loadErrors.join(" | ");
      }
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    MagickWandGenesis_();
    loaded_ = true;
    return true;
  }

  bool writeBlob(const QByteArray& data,
                 const char* format,
                 const bool webpOptions,
                 QByteArray* outputData,
                 QString* errorMessage) {
    if (!ensureLoaded(errorMessage)) {
      return false;
    }
    if (data.isEmpty()) {
      setError(errorMessage, "图片数据为空。");
      return false;
    }
    if (!outputData) {
      setError(errorMessage, "输出缓冲区无效。");
      return false;
    }

    MagickWand* wand = NewMagickWand_();
    if (!wand) {
      setError(errorMessage, "创建 MagickWand 失败。");
      return false;
    }

    const MagickBooleanType readOk = MagickReadImageBlob_(
        wand, data.constData(), static_cast<size_t>(data.size()));
    if (!readOk) {
      const QString error = wandException(wand);
      DestroyMagickWand_(wand);
      setError(errorMessage, "读取图片失败: " + error);
      return false;
    }

    if (webpOptions) {
      MagickSetOption_(wand, "webp:lossless", "false");
      MagickSetOption_(wand, "webp:method", "6");
      MagickSetOption_(wand, "webp:alpha-quality", "85");
      MagickSetImageCompressionQuality_(wand, 85);
    }

    if (!MagickSetImageFormat_(wand, format)) {
      const QString error = wandException(wand);
      DestroyMagickWand_(wand);
      setError(errorMessage, "设置输出格式失败: " + error);
      return false;
    }

    size_t outputSize = 0;
    unsigned char* blob = MagickGetImagesBlob_(wand, &outputSize);
    if (!blob || outputSize == 0) {
      const QString error = wandException(wand);
      if (blob) {
        MagickRelinquishMemory_(blob);
      }
      DestroyMagickWand_(wand);
      setError(errorMessage, "生成输出数据失败: " + error);
      return false;
    }

    *outputData = QByteArray(reinterpret_cast<const char*>(blob),
                             static_cast<int>(outputSize));
    MagickRelinquishMemory_(blob);
    DestroyMagickWand_(wand);
    return !outputData->isEmpty();
  }

 private:
  using MagickWandGenesisFunc = void (*)();
  using MagickWandTerminusFunc = void (*)();
  using NewMagickWandFunc = MagickWand* (*)();
  using DestroyMagickWandFunc = MagickWand* (*)(MagickWand*);
  using MagickReadImageBlobFunc =
      MagickBooleanType (*)(MagickWand*, const void*, size_t);
  using MagickSetImageFormatFunc =
      MagickBooleanType (*)(MagickWand*, const char*);
  using MagickSetOptionFunc =
      MagickBooleanType (*)(MagickWand*, const char*, const char*);
  using MagickSetImageCompressionQualityFunc =
      MagickBooleanType (*)(MagickWand*, size_t);
  using MagickGetImagesBlobFunc = unsigned char* (*)(MagickWand*, size_t*);
  using MagickGetExceptionFunc = char* (*)(MagickWand*, ExceptionType*);
  using MagickRelinquishMemoryFunc = void* (*)(void*);

  bool resolveAll(QString* errorMessage) {
#define RESOLVE_MAGICK_SYMBOL(name)                                           \
  do {                                                                        \
    name##_ = reinterpret_cast<name##Func>(library_.resolve(#name));          \
    if (!name##_) {                                                           \
      *errorMessage = QString("ImageMagick 运行库缺少符号: ") + #name;     \
      return false;                                                           \
    }                                                                         \
  } while (false)

    RESOLVE_MAGICK_SYMBOL(MagickWandGenesis);
    RESOLVE_MAGICK_SYMBOL(MagickWandTerminus);
    RESOLVE_MAGICK_SYMBOL(NewMagickWand);
    RESOLVE_MAGICK_SYMBOL(DestroyMagickWand);
    RESOLVE_MAGICK_SYMBOL(MagickReadImageBlob);
    RESOLVE_MAGICK_SYMBOL(MagickSetImageFormat);
    RESOLVE_MAGICK_SYMBOL(MagickSetOption);
    RESOLVE_MAGICK_SYMBOL(MagickSetImageCompressionQuality);
    RESOLVE_MAGICK_SYMBOL(MagickGetImagesBlob);
    RESOLVE_MAGICK_SYMBOL(MagickGetException);
    RESOLVE_MAGICK_SYMBOL(MagickRelinquishMemory);

#undef RESOLVE_MAGICK_SYMBOL
    return true;
  }

  QString wandException(MagickWand* wand) {
    ExceptionType exceptionType = 0;
    char* text = MagickGetException_(wand, &exceptionType);
    if (!text) {
      return "未知 ImageMagick 错误。";
    }
    const QString result = QString::fromLocal8Bit(text).trimmed();
    MagickRelinquishMemory_(text);
    return result.isEmpty() ? "未知 ImageMagick 错误。" : result;
  }

  static void setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
      *errorMessage = message;
    }
  }

  QMutex mutex_;
  QLibrary library_;
  bool attempted_{};
  bool loaded_{};
  QString loadError_;
  QString loadedLibraryName_;

  MagickWandGenesisFunc MagickWandGenesis_{};
  MagickWandTerminusFunc MagickWandTerminus_{};
  NewMagickWandFunc NewMagickWand_{};
  DestroyMagickWandFunc DestroyMagickWand_{};
  MagickReadImageBlobFunc MagickReadImageBlob_{};
  MagickSetImageFormatFunc MagickSetImageFormat_{};
  MagickSetOptionFunc MagickSetOption_{};
  MagickSetImageCompressionQualityFunc MagickSetImageCompressionQuality_{};
  MagickGetImagesBlobFunc MagickGetImagesBlob_{};
  MagickGetExceptionFunc MagickGetException_{};
  MagickRelinquishMemoryFunc MagickRelinquishMemory_{};
};

MagickWandApi& magickApi() {
  static MagickWandApi instance;
  return instance;
}

}  // namespace

bool MagickWandBridge::decodeToPng(const QByteArray& data,
                                   QByteArray* pngData,
                                   QString* errorMessage) {
  return magickApi().writeBlob(data, "PNG32", false, pngData, errorMessage);
}

bool MagickWandBridge::convertToWebp(const QByteArray& data,
                                     QByteArray* webpData,
                                     QString* errorMessage) {
  return magickApi().writeBlob(data, "WEBP", true, webpData, errorMessage);
}
