#include "preview/webp_image_encoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QtGlobal>

#include <cstdint>
#include <cstring>

#include "core/runtime_paths.h"

namespace {

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

QStringList webpLibraryCandidates(const QString& runtimeDir) {
  QStringList candidates;
#ifdef Q_OS_WIN
  const QStringList names = {
      "libwebp.dll",
      "libwebp-7.dll",
      "CORE_RL_webp_.dll",
  };
  if (!runtimeDir.isEmpty()) {
    for (const QString& name : names) {
      appendUnique(&candidates, QDir(runtimeDir).filePath(name));
    }
  }
  for (const QString& name : names) {
    appendUnique(&candidates,
                 QDir(QCoreApplication::applicationDirPath()).filePath(name));
  }
#else
  const QStringList filters = {
      "libwebp.so*",
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
  appendUnique(&candidates, "webp");
#endif
  return candidates;
}

class WebpApi {
 public:
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

    const QStringList candidates = webpLibraryCandidates(runtimeDir);
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
        loaded_ = true;
        return true;
      }

      loadErrors.push_back(candidate + ": " + symbolError);
      library_.unload();
    }

    loadError_ = "加载 libwebp 运行库失败。";
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

  using WebPEncodeRGBAFunc =
      size_t (*)(const uint8_t*, int, int, int, float, uint8_t**);
  using WebPDecodeRGBAFunc = uint8_t* (*)(const uint8_t*, size_t, int*, int*);
  using WebPGetInfoFunc = int (*)(const uint8_t*, size_t, int*, int*);
  using WebPFreeFunc = void (*)(void*);
  using WebPGetEncoderVersionFunc = int (*)();

  WebPEncodeRGBAFunc WebPEncodeRGBA_{};
  WebPDecodeRGBAFunc WebPDecodeRGBA_{};
  WebPGetInfoFunc WebPGetInfo_{};
  WebPFreeFunc WebPFree_{};
  WebPGetEncoderVersionFunc WebPGetEncoderVersion_{};

 private:
  bool resolveAll(QString* errorMessage) {
    WebPEncodeRGBA_ = reinterpret_cast<WebPEncodeRGBAFunc>(
        library_.resolve("WebPEncodeRGBA"));
    WebPDecodeRGBA_ = reinterpret_cast<WebPDecodeRGBAFunc>(
        library_.resolve("WebPDecodeRGBA"));
    WebPGetInfo_ =
        reinterpret_cast<WebPGetInfoFunc>(library_.resolve("WebPGetInfo"));
    WebPFree_ = reinterpret_cast<WebPFreeFunc>(library_.resolve("WebPFree"));
    WebPGetEncoderVersion_ = reinterpret_cast<WebPGetEncoderVersionFunc>(
        library_.resolve("WebPGetEncoderVersion"));
    if (!WebPEncodeRGBA_) {
      *errorMessage = "libwebp 运行库缺少符号: WebPEncodeRGBA";
      return false;
    }
    if (!WebPDecodeRGBA_) {
      *errorMessage = "libwebp 运行库缺少符号: WebPDecodeRGBA";
      return false;
    }
    if (!WebPGetInfo_) {
      *errorMessage = "libwebp 运行库缺少符号: WebPGetInfo";
      return false;
    }
    if (!WebPFree_) {
      *errorMessage = "libwebp 运行库缺少符号: WebPFree";
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

WebpApi& webpApi() {
  static WebpApi instance;
  return instance;
}

}  // namespace

bool WebpImageEncoder::decode(const QByteArray& data,
                              QImage* image,
                              QString* errorMessage) {
  if (image) {
    *image = QImage();
  }
  if (!image) {
    return false;
  }
  if (data.isEmpty()) {
    if (errorMessage) {
      *errorMessage = "WebP 数据为空。";
    }
    return false;
  }

  WebpApi& api = webpApi();
  QString loadError;
  if (!api.ensureLoaded(&loadError)) {
    if (errorMessage) {
      *errorMessage = loadError;
    }
    return false;
  }

  int width = 0;
  int height = 0;
  if (!api.WebPGetInfo_(reinterpret_cast<const uint8_t*>(data.constData()),
                        static_cast<size_t>(data.size()), &width, &height) ||
      width <= 0 || height <= 0) {
    if (errorMessage) {
      *errorMessage = "libwebp 无法识别 WebP 图像尺寸。";
    }
    return false;
  }

  int decodedWidth = 0;
  int decodedHeight = 0;
  uint8_t* rgba = api.WebPDecodeRGBA_(
      reinterpret_cast<const uint8_t*>(data.constData()),
      static_cast<size_t>(data.size()), &decodedWidth, &decodedHeight);
  if (!rgba || decodedWidth <= 0 || decodedHeight <= 0) {
    if (rgba) {
      api.WebPFree_(rgba);
    }
    if (errorMessage) {
      *errorMessage = "libwebp 解码 WebP 失败。";
    }
    return false;
  }

  QImage decoded(decodedWidth, decodedHeight, QImage::Format_RGBA8888);
  if (decoded.isNull()) {
    api.WebPFree_(rgba);
    if (errorMessage) {
      *errorMessage = "创建 WebP 解码图像缓冲失败。";
    }
    return false;
  }

  const int stride = decodedWidth * 4;
  for (int y = 0; y < decodedHeight; ++y) {
    std::memcpy(decoded.scanLine(y), rgba + static_cast<qsizetype>(y) * stride,
                static_cast<size_t>(stride));
  }
  api.WebPFree_(rgba);

  *image = decoded;
  return true;
}

bool WebpImageEncoder::encode(const QImage& image,
                              int quality,
                              QByteArray* webpData,
                              QString* errorMessage) {
  if (webpData) {
    webpData->clear();
  }
  if (!webpData) {
    return false;
  }
  if (image.isNull()) {
    if (errorMessage) {
      *errorMessage = "图片为空，无法编码 WebP。";
    }
    return false;
  }

  WebpApi& api = webpApi();
  QString loadError;
  if (!api.ensureLoaded(&loadError)) {
    if (errorMessage) {
      *errorMessage = loadError;
    }
    return false;
  }

  const int clampedQuality = qBound(1, quality, 100);
  const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
  if (rgba.isNull()) {
    if (errorMessage) {
      *errorMessage = "无法把图片转换为 RGBA 像素缓冲。";
    }
    return false;
  }

  uint8_t* output = nullptr;
  const size_t outputSize = api.WebPEncodeRGBA_(
      rgba.constBits(), rgba.width(), rgba.height(), rgba.bytesPerLine(),
      static_cast<float>(clampedQuality), &output);
  if (!output || outputSize == 0) {
    if (output) {
      api.WebPFree_(output);
    }
    if (errorMessage) {
      *errorMessage = "libwebp 编码结果为空。";
    }
    return false;
  }

  webpData->resize(static_cast<int>(outputSize));
  std::memcpy(webpData->data(), output, outputSize);
  api.WebPFree_(output);
  return true;
}
