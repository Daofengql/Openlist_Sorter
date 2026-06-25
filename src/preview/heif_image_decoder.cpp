#include "preview/heif_image_decoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <cstring>

#include "core/runtime_paths.h"

namespace {

struct heif_context;
struct heif_image_handle;
struct heif_image;
struct heif_security_limits;
struct heif_reading_options;
struct heif_decoding_options;
struct heif_init_params;

enum heif_error_code {
  heif_error_Ok = 0,
};

enum heif_suberror_code {
  heif_suberror_Unspecified = 0,
};

struct heif_error {
  heif_error_code code;
  heif_suberror_code subcode;
  const char* message;
};

enum heif_colorspace {
  heif_colorspace_RGB = 1,
};

enum heif_chroma {
  heif_chroma_interleaved_RGBA = 11,
};

enum heif_channel {
  heif_channel_interleaved = 10,
};

QString errorText(const heif_error& error) {
  QString message = error.message ? QString::fromUtf8(error.message).trimmed()
                                  : QString();
  if (message.isEmpty()) {
    message = "未知 libheif 错误";
  }
  return QString("%1 (code=%2, subcode=%3)")
      .arg(message)
      .arg(static_cast<int>(error.code))
      .arg(static_cast<int>(error.subcode));
}

bool isOk(const heif_error& error) {
  return error.code == heif_error_Ok;
}

ImageDecodeResult makeFailure(const QString& message) {
  return {false, {}, message, "libheif"};
}

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

QStringList heifLibraryCandidates(const QString& runtimeDir) {
  QStringList candidates;
#ifdef Q_OS_WIN
  if (!runtimeDir.isEmpty()) {
    appendUnique(&candidates, QDir(runtimeDir).filePath("libheif.dll"));
    appendUnique(&candidates, QDir(runtimeDir).filePath("libheif-1.dll"));
    appendUnique(&candidates, QDir(runtimeDir).filePath("CORE_RL_heif_.dll"));
  }
#else
  const QStringList filters = {
      "libheif.so*",
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

  appendUnique(&candidates, "heif");
#endif
  return candidates;
}

class HeifApi {
 public:
  ~HeifApi() {
    if (loaded_ && heif_deinit_) {
      heif_deinit_();
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

    const QString runtimeDir = RuntimePaths::runtimeDirectory("imagecodecs");
    if (!runtimeDir.isEmpty()) {
      RuntimePaths::addDllSearchPath(runtimeDir);
      RuntimePaths::addDllSearchPath(QDir(runtimeDir).filePath("lib"));
    }

    const QStringList candidates = heifLibraryCandidates(runtimeDir);
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
      loadError_ = "加载 libheif 运行库失败。";
      if (runtimeDir.isEmpty()) {
        loadError_ += " 未找到 runtime/imagecodecs 或 "
                      "third_party/runtime/imagecodecs。";
      }
      if (!loadErrors.isEmpty()) {
        loadError_ += " 尝试结果: " + loadErrors.join(" | ");
      }
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    if (heif_init_) {
      const heif_error initError = heif_init_(nullptr);
      if (!isOk(initError)) {
        loadError_ = "初始化 libheif 失败: " + errorText(initError);
        library_.unload();
        if (errorMessage) {
          *errorMessage = loadError_;
        }
        return false;
      }
    }

    loaded_ = true;
    return true;
  }

  using HeifInitFunc = heif_error (*)(heif_init_params*);
  using HeifDeinitFunc = void (*)();
  using HeifGetVersionFunc = const char* (*)();
  using HeifContextAllocFunc = heif_context* (*)();
  using HeifContextFreeFunc = void (*)(heif_context*);
  using HeifContextReadFromMemoryWithoutCopyFunc =
      heif_error (*)(heif_context*, const void*, size_t,
                     const heif_reading_options*);
  using HeifContextGetPrimaryImageHandleFunc =
      heif_error (*)(heif_context*, heif_image_handle**);
  using HeifContextSetSecurityLimitsFunc =
      heif_error (*)(heif_context*, const heif_security_limits*);
  using HeifContextSetMaximumImageSizeLimitFunc =
      void (*)(heif_context*, int);
  using HeifGetDisabledSecurityLimitsFunc =
      const heif_security_limits* (*)();
  using HeifImageHandleGetWidthFunc = int (*)(const heif_image_handle*);
  using HeifImageHandleGetHeightFunc = int (*)(const heif_image_handle*);
  using HeifImageHandleReleaseFunc = void (*)(const heif_image_handle*);
  using HeifDecodeImageFunc =
      heif_error (*)(const heif_image_handle*, heif_image**, heif_colorspace,
                     heif_chroma, const heif_decoding_options*);
  using HeifImageGetPrimaryWidthFunc = int (*)(const heif_image*);
  using HeifImageGetPrimaryHeightFunc = int (*)(const heif_image*);
  using HeifImageGetPlaneReadonlyFunc =
      const unsigned char* (*)(const heif_image*, heif_channel, int*);
  using HeifImageReleaseFunc = void (*)(const heif_image*);
  using HeifImageGetWidthFunc = int (*)(const heif_image*, heif_channel);
  using HeifImageGetHeightFunc = int (*)(const heif_image*, heif_channel);

  HeifInitFunc heif_init_{};
  HeifDeinitFunc heif_deinit_{};
  HeifGetVersionFunc heif_get_version_{};
  HeifContextAllocFunc heif_context_alloc_{};
  HeifContextFreeFunc heif_context_free_{};
  HeifContextReadFromMemoryWithoutCopyFunc
      heif_context_read_from_memory_without_copy_{};
  HeifContextGetPrimaryImageHandleFunc
      heif_context_get_primary_image_handle_{};
  HeifContextSetSecurityLimitsFunc heif_context_set_security_limits_{};
  HeifContextSetMaximumImageSizeLimitFunc
      heif_context_set_maximum_image_size_limit_{};
  HeifGetDisabledSecurityLimitsFunc heif_get_disabled_security_limits_{};
  HeifImageHandleGetWidthFunc heif_image_handle_get_width_{};
  HeifImageHandleGetHeightFunc heif_image_handle_get_height_{};
  HeifImageHandleReleaseFunc heif_image_handle_release_{};
  HeifDecodeImageFunc heif_decode_image_{};
  HeifImageGetPrimaryWidthFunc heif_image_get_primary_width_{};
  HeifImageGetPrimaryHeightFunc heif_image_get_primary_height_{};
  HeifImageGetPlaneReadonlyFunc heif_image_get_plane_readonly_{};
  HeifImageReleaseFunc heif_image_release_{};
  HeifImageGetWidthFunc heif_image_get_width_{};
  HeifImageGetHeightFunc heif_image_get_height_{};

 private:
  bool resolveAll(QString* errorMessage) {
#define RESOLVE_HEIF_SYMBOL(name)                                             \
  do {                                                                        \
    name##_ = reinterpret_cast<decltype(name##_)>(library_.resolve(#name));   \
    if (!name##_) {                                                           \
      *errorMessage = QString("libheif 运行库缺少符号: ") + #name;         \
      return false;                                                           \
    }                                                                         \
  } while (false)

    RESOLVE_HEIF_SYMBOL(heif_get_version);
    RESOLVE_HEIF_SYMBOL(heif_context_alloc);
    RESOLVE_HEIF_SYMBOL(heif_context_free);
    RESOLVE_HEIF_SYMBOL(heif_context_read_from_memory_without_copy);
    RESOLVE_HEIF_SYMBOL(heif_context_get_primary_image_handle);
    RESOLVE_HEIF_SYMBOL(heif_image_handle_get_width);
    RESOLVE_HEIF_SYMBOL(heif_image_handle_get_height);
    RESOLVE_HEIF_SYMBOL(heif_image_handle_release);
    RESOLVE_HEIF_SYMBOL(heif_decode_image);
    RESOLVE_HEIF_SYMBOL(heif_image_get_plane_readonly);
    RESOLVE_HEIF_SYMBOL(heif_image_release);

#undef RESOLVE_HEIF_SYMBOL

    heif_init_ = reinterpret_cast<HeifInitFunc>(library_.resolve("heif_init"));
    heif_deinit_ =
        reinterpret_cast<HeifDeinitFunc>(library_.resolve("heif_deinit"));
    heif_context_set_security_limits_ =
        reinterpret_cast<HeifContextSetSecurityLimitsFunc>(
            library_.resolve("heif_context_set_security_limits"));
    heif_context_set_maximum_image_size_limit_ =
        reinterpret_cast<HeifContextSetMaximumImageSizeLimitFunc>(
            library_.resolve("heif_context_set_maximum_image_size_limit"));
    heif_get_disabled_security_limits_ =
        reinterpret_cast<HeifGetDisabledSecurityLimitsFunc>(
            library_.resolve("heif_get_disabled_security_limits"));
    heif_image_get_width_ =
        reinterpret_cast<HeifImageGetWidthFunc>(
            library_.resolve("heif_image_get_width"));
    heif_image_get_height_ =
        reinterpret_cast<HeifImageGetHeightFunc>(
            library_.resolve("heif_image_get_height"));
    heif_image_get_primary_width_ =
        reinterpret_cast<HeifImageGetPrimaryWidthFunc>(
            library_.resolve("heif_image_get_primary_width"));
    heif_image_get_primary_height_ =
        reinterpret_cast<HeifImageGetPrimaryHeightFunc>(
            library_.resolve("heif_image_get_primary_height"));
    if ((!heif_image_get_primary_width_ || !heif_image_get_primary_height_) &&
        (!heif_image_get_width_ || !heif_image_get_height_)) {
      *errorMessage =
          "libheif 运行库缺少图像尺寸符号: heif_image_get_primary_width/"
          "heif_image_get_primary_height 或 heif_image_get_width/"
          "heif_image_get_height";
      return false;
    }
    return true;
  }

  QMutex mutex_;
  QLibrary library_;
  bool attempted_{};
  bool loaded_{};
  QString loadError_;
  QString loadedLibraryName_;
};

HeifApi& heifApi() {
  static HeifApi instance;
  return instance;
}

class ScopedHeifContext {
 public:
  explicit ScopedHeifContext(HeifApi* api) : api_(api) {
    context_ = api_->heif_context_alloc_();
  }

  ~ScopedHeifContext() {
    if (context_) {
      api_->heif_context_free_(context_);
    }
  }

  ScopedHeifContext(const ScopedHeifContext&) = delete;
  ScopedHeifContext& operator=(const ScopedHeifContext&) = delete;

  heif_context* get() const { return context_; }

 private:
  HeifApi* api_{};
  heif_context* context_{};
};

class ScopedHeifHandle {
 public:
  ScopedHeifHandle(HeifApi* api, heif_image_handle* handle)
      : api_(api), handle_(handle) {}

  ~ScopedHeifHandle() {
    if (handle_) {
      api_->heif_image_handle_release_(handle_);
    }
  }

  ScopedHeifHandle(const ScopedHeifHandle&) = delete;
  ScopedHeifHandle& operator=(const ScopedHeifHandle&) = delete;

  heif_image_handle* get() const { return handle_; }

 private:
  HeifApi* api_{};
  heif_image_handle* handle_{};
};

class ScopedHeifImage {
 public:
  ScopedHeifImage(HeifApi* api, heif_image* image)
      : api_(api), image_(image) {}

  ~ScopedHeifImage() {
    if (image_) {
      api_->heif_image_release_(image_);
    }
  }

  ScopedHeifImage(const ScopedHeifImage&) = delete;
  ScopedHeifImage& operator=(const ScopedHeifImage&) = delete;

  heif_image* get() const { return image_; }

 private:
  HeifApi* api_{};
  heif_image* image_{};
};

ImageDecodeResult decodeWithLibheif(const QByteArray& data,
                                    const QString& qtError,
                                    const QString& fallbackError) {
  HeifApi& api = heifApi();
  QString loadError;
  if (!api.ensureLoaded(&loadError)) {
    return makeFailure("当前格式需要 libheif，但 libheif "
                       "不可用: " +
                       loadError + "。Qt 错误: " + qtError +
                       "。兜底信息: " + fallbackError);
  }

  if (data.isEmpty()) {
    return makeFailure("图片数据为空，无法交给 libheif 解码。");
  }

  ScopedHeifContext context(&api);
  if (!context.get()) {
    return makeFailure("libheif 创建上下文失败。");
  }

  const heif_security_limits* disabledLimits =
      api.heif_get_disabled_security_limits_
          ? api.heif_get_disabled_security_limits_()
          : nullptr;
  if (disabledLimits && api.heif_context_set_security_limits_) {
    const heif_error limitError =
        api.heif_context_set_security_limits_(context.get(), disabledLimits);
    if (!isOk(limitError)) {
      return makeFailure("libheif 设置安全限制失败: " + errorText(limitError));
    }
  } else if (api.heif_context_set_maximum_image_size_limit_) {
    api.heif_context_set_maximum_image_size_limit_(context.get(), 65535);
  }

  const heif_error readError = api.heif_context_read_from_memory_without_copy_(
      context.get(), data.constData(), static_cast<size_t>(data.size()),
      nullptr);
  if (!isOk(readError)) {
    return makeFailure("libheif 读取内存图片失败: " + errorText(readError));
  }

  heif_image_handle* rawHandle = nullptr;
  const heif_error handleError =
      api.heif_context_get_primary_image_handle_(context.get(), &rawHandle);
  if (!isOk(handleError)) {
    return makeFailure("libheif 获取主图失败: " + errorText(handleError));
  }
  ScopedHeifHandle handle(&api, rawHandle);

  const int expectedWidth = api.heif_image_handle_get_width_(handle.get());
  const int expectedHeight = api.heif_image_handle_get_height_(handle.get());
  if (expectedWidth <= 0 || expectedHeight <= 0) {
    return makeFailure("libheif 主图尺寸无效。");
  }

  heif_image* rawImage = nullptr;
  const heif_error decodeError =
      api.heif_decode_image_(handle.get(), &rawImage, heif_colorspace_RGB,
                             heif_chroma_interleaved_RGBA, nullptr);
  if (!isOk(decodeError)) {
    return makeFailure("libheif 解码主图失败: " + errorText(decodeError));
  }
  ScopedHeifImage decoded(&api, rawImage);

  const int width = api.heif_image_get_primary_width_
                        ? api.heif_image_get_primary_width_(decoded.get())
                        : (api.heif_image_get_width_
                               ? api.heif_image_get_width_(
                                     decoded.get(), heif_channel_interleaved)
                               : expectedWidth);
  const int height = api.heif_image_get_primary_height_
                         ? api.heif_image_get_primary_height_(decoded.get())
                         : (api.heif_image_get_height_
                                ? api.heif_image_get_height_(
                                      decoded.get(), heif_channel_interleaved)
                                : expectedHeight);
  if (width <= 0 || height <= 0) {
    return makeFailure("libheif 解码后图像尺寸无效。");
  }

  int stride = 0;
  const unsigned char* rgba = api.heif_image_get_plane_readonly_(
      decoded.get(), heif_channel_interleaved, &stride);
  if (!rgba || stride <= 0) {
    return makeFailure("libheif 解码后没有 RGBA 像素平面。");
  }

  const int minStride = width * 4;
  if (stride < minStride) {
    return makeFailure("libheif RGBA 行跨度异常。");
  }

  QImage image(width, height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return makeFailure(
        QString("libheif 已解码，但创建 Qt 图像缓冲区失败: %1x%2")
            .arg(width)
            .arg(height));
  }

  for (int y = 0; y < height; ++y) {
    std::memcpy(image.scanLine(y), rgba + static_cast<qsizetype>(y) * stride,
                static_cast<size_t>(minStride));
  }

  const char* version = api.heif_get_version_();
  const QString decoder =
      QString("libheif%1 (%2x%3)")
          .arg(version ? QString(" ") + QString::fromUtf8(version) : QString())
          .arg(width)
          .arg(height);
  const QString message =
      QString("libheif 直接内存解码，源主图 %1x%2")
          .arg(expectedWidth)
          .arg(expectedHeight);
  return {true, image, message, decoder};
}

}  // namespace

ImageDecodeResult HeifImageDecoder::decodeToImage(
    const QByteArray& data,
    const QString& sourceName,
    const QString& qtError,
    const QString& imageMagickError) {
  Q_UNUSED(sourceName);
  return decodeWithLibheif(data, qtError, imageMagickError);
}
