#include "preview/magick_wand_bridge.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>

#include "core/runtime_paths.h"

namespace {

using MagickBooleanType = int;
using ExceptionType = int;
using MagickWand = void;

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
    if (runtimeDir.isEmpty()) {
      loadError_ =
          "未找到 ImageMagick DLL 目录 runtime/imagemagick 或 "
          "third_party/runtime/imagemagick。";
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    const QString wandPath =
        QDir(runtimeDir).filePath("CORE_RL_MagickWand_.dll");
    if (!QFileInfo::exists(wandPath)) {
      loadError_ = "ImageMagick 目录缺少 CORE_RL_MagickWand_.dll: " +
                   QDir::toNativeSeparators(runtimeDir);
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    RuntimePaths::addDllSearchPath(runtimeDir);
    qputenv("MAGICK_HOME", QDir::toNativeSeparators(runtimeDir).toLocal8Bit());
    qputenv("MAGICK_CONFIGURE_PATH",
            QDir::toNativeSeparators(runtimeDir).toLocal8Bit());
    qputenv("MAGICK_CODER_MODULE_PATH",
            QDir::toNativeSeparators(QDir(runtimeDir).filePath("modules/coders"))
                .toLocal8Bit());
    qputenv("MAGICK_FILTER_MODULE_PATH",
            QDir::toNativeSeparators(
                QDir(runtimeDir).filePath("modules/filters"))
                .toLocal8Bit());

    library_.setFileName(wandPath);
    if (!library_.load()) {
      loadError_ = "加载 ImageMagick DLL 失败: " + library_.errorString();
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    if (!resolveAll(&loadError_)) {
      library_.unload();
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
      *errorMessage = QString("ImageMagick DLL 缺少符号: ") + #name;         \
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
