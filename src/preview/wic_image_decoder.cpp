#include "preview/wic_image_decoder.h"

#include <QImage>

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

ImageDecodeResult makeFailure(const QString& message) {
  return {false, {}, message, "Windows Imaging Component"};
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

#endif

}  // namespace

ImageDecodeResult WicImageDecoder::decodeToImage(const QByteArray& data,
                                                 const QString& sourceName) {
  Q_UNUSED(sourceName);
#ifndef Q_OS_WIN
  Q_UNUSED(data);
  return makeFailure("当前平台没有启用 WIC 解码。");
#else
  if (data.isEmpty()) {
    return makeFailure("图片数据为空。");
  }

  ScopedComInit com;
  if (!com.ok()) {
    return makeFailure("初始化 COM 失败: " + hresultText(com.result()));
  }

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                reinterpret_cast<void**>(factory.put()));
  if (FAILED(hr) || !factory.get()) {
    return makeFailure("创建 WIC 工厂失败: " + hresultText(hr));
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(stream.put());
  if (FAILED(hr) || !stream.get()) {
    return makeFailure("创建 WIC 内存流失败: " + hresultText(hr));
  }

  hr = stream->InitializeFromMemory(
      reinterpret_cast<BYTE*>(const_cast<char*>(data.constData())),
      static_cast<DWORD>(data.size()));
  if (FAILED(hr)) {
    return makeFailure("初始化 WIC 内存流失败: " + hresultText(hr));
  }

  ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromStream(
      stream.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put());
  if (FAILED(hr) || !decoder.get()) {
    return makeFailure("WIC 无法识别该图片: " + hresultText(hr));
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, frame.put());
  if (FAILED(hr) || !frame.get()) {
    return makeFailure("读取 WIC 首帧失败: " + hresultText(hr));
  }

  UINT width = 0;
  UINT height = 0;
  hr = frame->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0 ||
      width > static_cast<UINT>(std::numeric_limits<int>::max()) ||
      height > static_cast<UINT>(std::numeric_limits<int>::max())) {
    return makeFailure("WIC 图像尺寸无效: " + hresultText(hr));
  }

  ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(converter.put());
  if (FAILED(hr) || !converter.get()) {
    return makeFailure("创建 WIC 像素转换器失败: " + hresultText(hr));
  }

  hr = converter->Initialize(frame.get(), GUID_WICPixelFormat32bppRGBA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    return makeFailure("WIC 无法转换为 RGBA: " + hresultText(hr));
  }

  QImage image(static_cast<int>(width), static_cast<int>(height),
               QImage::Format_RGBA8888);
  if (image.isNull()) {
    return makeFailure("创建 WIC 输出图像缓冲失败。");
  }

  hr = converter->CopyPixels(nullptr, static_cast<UINT>(image.bytesPerLine()),
                             static_cast<UINT>(image.bytesPerLine() *
                                               image.height()),
                             image.bits());
  if (FAILED(hr)) {
    return makeFailure("WIC 拷贝像素失败: " + hresultText(hr));
  }

  return {true, image, "WIC 系统底层解码", "Windows Imaging Component"};
#endif
}
