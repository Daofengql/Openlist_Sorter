#include "preview/jpeg_image_encoder.h"

#include <QBuffer>
#include <QtGlobal>

#include <cstring>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>
#include <wincodec.h>
#include <windows.h>
#endif

namespace {

bool fail(const QString& message, QString* errorMessage) {
  if (errorMessage) {
    *errorMessage = message;
  }
  return false;
}

#ifdef Q_OS_WIN

QString hresultText(HRESULT hr) {
  return "HRESULT=0x" +
         QString::number(static_cast<qulonglong>(static_cast<ULONG>(hr)), 16)
             .rightJustified(8, '0');
}

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ~ComPtr() { reset(); }

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  T** put() {
    reset();
    return &value_;
  }

  T* get() const { return value_; }
  T* operator->() const { return value_; }

 private:
  void reset() {
    if (value_) {
      value_->Release();
      value_ = nullptr;
    }
  }

  T* value_{};
};

class ScopedComInit {
 public:
  ScopedComInit() {
    result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = result_ == S_OK || result_ == S_FALSE;
  }

  ~ScopedComInit() {
    if (initialized_) {
      CoUninitialize();
    }
  }

  bool ok() const {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

  HRESULT result() const { return result_; }

 private:
  HRESULT result_{E_FAIL};
  bool initialized_{};
};

bool readStreamToBytes(IStream* stream,
                       QByteArray* jpegData,
                       QString* errorMessage) {
  if (!stream || !jpegData) {
    return fail("WIC JPEG 输出流无效。", errorMessage);
  }

  STATSTG stats = {};
  HRESULT hr = stream->Stat(&stats, STATFLAG_NONAME);
  if (FAILED(hr)) {
    return fail("读取 WIC JPEG 输出大小失败: " + hresultText(hr), errorMessage);
  }
  if (stats.cbSize.QuadPart <= 0 ||
      stats.cbSize.QuadPart >
          static_cast<ULONGLONG>(std::numeric_limits<int>::max())) {
    return fail("WIC JPEG 输出大小无效。", errorMessage);
  }

  LARGE_INTEGER start = {};
  hr = stream->Seek(start, STREAM_SEEK_SET, nullptr);
  if (FAILED(hr)) {
    return fail("重置 WIC JPEG 输出流失败: " + hresultText(hr), errorMessage);
  }

  jpegData->resize(static_cast<int>(stats.cbSize.QuadPart));
  ULONG bytesRead = 0;
  hr = stream->Read(jpegData->data(), static_cast<ULONG>(jpegData->size()),
                    &bytesRead);
  if (FAILED(hr) || bytesRead != static_cast<ULONG>(jpegData->size())) {
    jpegData->clear();
    return fail("读取 WIC JPEG 输出数据失败: " + hresultText(hr), errorMessage);
  }

  return true;
}

#endif

}  // namespace

bool JpegImageEncoder::encode(const QImage& image,
                              int quality,
                              QByteArray* jpegData,
                              QString* errorMessage) {
  if (jpegData) {
    jpegData->clear();
  }
  if (!jpegData) {
    return false;
  }
  if (image.isNull()) {
    return fail("图片为空，无法编码 JPG。", errorMessage);
  }

  const int clampedQuality = qBound(1, quality, 100);

#ifndef Q_OS_WIN
  QBuffer buffer(jpegData);
  if (!buffer.open(QIODevice::WriteOnly)) {
    return fail("创建 JPG 内存缓冲失败。", errorMessage);
  }
  if (!image.save(&buffer, "JPEG", clampedQuality)) {
    jpegData->clear();
    return fail("Qt JPG 编码失败。", errorMessage);
  }
  return !jpegData->isEmpty();
#else
  ScopedComInit com;
  if (!com.ok()) {
    return fail("初始化 COM 失败: " + hresultText(com.result()), errorMessage);
  }

  if (image.width() <= 0 || image.height() <= 0 ||
      image.width() > std::numeric_limits<UINT>::max() ||
      image.height() > std::numeric_limits<UINT>::max()) {
    return fail("JPG 图像尺寸无效。", errorMessage);
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(factory.put()));
  if (FAILED(hr) || !factory.get()) {
    return fail("创建 WIC 工厂失败: " + hresultText(hr), errorMessage);
  }

  ComPtr<IStream> outputStream;
  hr = CreateStreamOnHGlobal(nullptr, TRUE, outputStream.put());
  if (FAILED(hr) || !outputStream.get()) {
    return fail("创建 WIC JPEG 内存流失败: " + hresultText(hr), errorMessage);
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr,
                              encoder.put());
  if (FAILED(hr) || !encoder.get()) {
    return fail("创建 WIC JPEG 编码器失败: " + hresultText(hr), errorMessage);
  }

  hr = encoder->Initialize(outputStream.get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return fail("初始化 WIC JPEG 编码器失败: " + hresultText(hr), errorMessage);
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(frame.put(), properties.put());
  if (FAILED(hr) || !frame.get()) {
    return fail("创建 WIC JPEG 帧失败: " + hresultText(hr), errorMessage);
  }

  if (properties.get()) {
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = static_cast<float>(clampedQuality) / 100.0f;
    properties->Write(1, &option, &value);
    VariantClear(&value);
  }

  hr = frame->Initialize(properties.get());
  if (FAILED(hr)) {
    return fail("初始化 WIC JPEG 帧失败: " + hresultText(hr), errorMessage);
  }

  hr = frame->SetSize(static_cast<UINT>(image.width()),
                      static_cast<UINT>(image.height()));
  if (FAILED(hr)) {
    return fail("设置 WIC JPEG 尺寸失败: " + hresultText(hr), errorMessage);
  }

  WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat24bppBGR;
  hr = frame->SetPixelFormat(&pixelFormat);
  if (FAILED(hr) || pixelFormat != GUID_WICPixelFormat24bppBGR) {
    return fail("设置 WIC JPEG 像素格式失败: " + hresultText(hr), errorMessage);
  }

  const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
  if (rgb.isNull()) {
    return fail("无法把图片转换为 RGB 像素缓冲。", errorMessage);
  }

  const int width = rgb.width();
  const int height = rgb.height();
  const int stride = width * 3;
  QByteArray bgr;
  bgr.resize(stride * height);
  for (int y = 0; y < height; ++y) {
    const uchar* src = rgb.constScanLine(y);
    uchar* dst = reinterpret_cast<uchar*>(bgr.data()) + y * stride;
    for (int x = 0; x < width; ++x) {
      dst[x * 3 + 0] = src[x * 3 + 2];
      dst[x * 3 + 1] = src[x * 3 + 1];
      dst[x * 3 + 2] = src[x * 3 + 0];
    }
  }

  hr = frame->WritePixels(static_cast<UINT>(height),
                          static_cast<UINT>(stride),
                          static_cast<UINT>(bgr.size()),
                          reinterpret_cast<BYTE*>(bgr.data()));
  if (FAILED(hr)) {
    return fail("写入 WIC JPEG 像素失败: " + hresultText(hr), errorMessage);
  }

  hr = frame->Commit();
  if (FAILED(hr)) {
    return fail("提交 WIC JPEG 帧失败: " + hresultText(hr), errorMessage);
  }
  hr = encoder->Commit();
  if (FAILED(hr)) {
    return fail("提交 WIC JPEG 编码器失败: " + hresultText(hr), errorMessage);
  }

  return readStreamToBytes(outputStream.get(), jpegData, errorMessage);
#endif
}
