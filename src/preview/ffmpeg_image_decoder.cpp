#include "preview/ffmpeg_image_decoder.h"

#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QMutex>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/runtime_paths.h"

#ifndef OPENLIST_SORTER_HAS_FFMPEG_DLL
#define OPENLIST_SORTER_HAS_FFMPEG_DLL 0
#endif

#if OPENLIST_SORTER_HAS_FFMPEG_DLL
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}
#endif

namespace {

#if OPENLIST_SORTER_HAS_FFMPEG_DLL

QString findRuntimeDll(const QString& binDir, const QString& prefix) {
  QDir dir(binDir);
  const QStringList matches =
      dir.entryList({prefix + "-*.dll", prefix + ".dll"}, QDir::Files,
                    QDir::Name);
  if (matches.isEmpty()) {
    return {};
  }
  return dir.filePath(matches.first());
}

QString missingDllMessage(const QString& binDir, const QString& label) {
  return "FFmpeg 目录缺少 " + label + " DLL: " +
         QDir::toNativeSeparators(binDir);
}

struct MemoryReader {
  const unsigned char* data{};
  qint64 size{};
  qint64 offset{};
};

struct StreamCandidate {
  int index{};
  int width{};
  int height{};
  qint64 area{};
};

int readPacket(void* opaque, unsigned char* buffer, int bufferSize) {
  auto* reader = static_cast<MemoryReader*>(opaque);
  if (!reader || reader->offset >= reader->size) {
    return AVERROR_EOF;
  }

  const qint64 remaining = reader->size - reader->offset;
  const int bytesToCopy =
      static_cast<int>(qMin<qint64>(remaining, bufferSize));
  memcpy(buffer, reader->data + reader->offset,
         static_cast<size_t>(bytesToCopy));
  reader->offset += bytesToCopy;
  return bytesToCopy;
}

qint64 seekPacket(void* opaque, qint64 offset, int whence) {
  auto* reader = static_cast<MemoryReader*>(opaque);
  if (!reader) {
    return AVERROR(EINVAL);
  }
  if (whence == AVSEEK_SIZE) {
    return reader->size;
  }

  const int origin = whence & 0xFFFF;
  qint64 target = 0;
  if (origin == SEEK_SET) {
    target = offset;
  } else if (origin == SEEK_CUR) {
    target = reader->offset + offset;
  } else if (origin == SEEK_END) {
    target = reader->size + offset;
  } else {
    return AVERROR(EINVAL);
  }

  if (target < 0 || target > reader->size) {
    return AVERROR(EINVAL);
  }
  reader->offset = target;
  return reader->offset;
}

class FfmpegApi {
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

    const QString runtimeDir = RuntimePaths::runtimeDirectory("ffmpeg");
    if (runtimeDir.isEmpty()) {
      loadError_ =
          "未找到 FFmpeg DLL 目录 runtime/ffmpeg 或 "
          "third_party/runtime/ffmpeg。";
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    const QString binDir = QDir(runtimeDir).filePath("bin");
    if (!QFileInfo::exists(binDir)) {
      loadError_ = "FFmpeg 目录缺少 bin 子目录: " +
                   QDir::toNativeSeparators(runtimeDir);
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    RuntimePaths::addDllSearchPath(binDir);

    if (!loadLibrary(&avutil_, findRuntimeDll(binDir, "avutil"), binDir,
                     "avutil", &loadError_) ||
        !loadLibrary(&swscale_, findRuntimeDll(binDir, "swscale"), binDir,
                     "swscale", &loadError_) ||
        !loadLibrary(&avcodec_, findRuntimeDll(binDir, "avcodec"), binDir,
                     "avcodec", &loadError_) ||
        !loadLibrary(&avformat_, findRuntimeDll(binDir, "avformat"), binDir,
                     "avformat", &loadError_)) {
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    if (!resolveAll(&loadError_)) {
      if (errorMessage) {
        *errorMessage = loadError_;
      }
      return false;
    }

    loaded_ = true;
    return true;
  }

  QString avError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror_(code, buffer, sizeof(buffer)) < 0) {
      return QString("FFmpeg 错误码 %1").arg(code);
    }
    return QString::fromLocal8Bit(buffer).trimmed();
  }

  decltype(&av_malloc) av_malloc_{};
  decltype(&av_free) av_free_{};
  decltype(&av_strerror) av_strerror_{};
  decltype(&av_frame_alloc) av_frame_alloc_{};
  decltype(&av_frame_free) av_frame_free_{};

  decltype(&avio_alloc_context) avio_alloc_context_{};
  decltype(&avio_context_free) avio_context_free_{};
  decltype(&avformat_alloc_context) avformat_alloc_context_{};
  decltype(&avformat_open_input) avformat_open_input_{};
  decltype(&avformat_find_stream_info) avformat_find_stream_info_{};
  decltype(&av_read_frame) av_read_frame_{};
  decltype(&avformat_close_input) avformat_close_input_{};

  decltype(&avcodec_find_decoder) avcodec_find_decoder_{};
  decltype(&avcodec_alloc_context3) avcodec_alloc_context3_{};
  decltype(&avcodec_parameters_to_context) avcodec_parameters_to_context_{};
  decltype(&avcodec_open2) avcodec_open2_{};
  decltype(&avcodec_send_packet) avcodec_send_packet_{};
  decltype(&avcodec_receive_frame) avcodec_receive_frame_{};
  decltype(&avcodec_free_context) avcodec_free_context_{};
  decltype(&av_packet_alloc) av_packet_alloc_{};
  decltype(&av_packet_unref) av_packet_unref_{};
  decltype(&av_packet_free) av_packet_free_{};

  decltype(&sws_getContext) sws_getContext_{};
  decltype(&sws_scale) sws_scale_{};
  decltype(&sws_freeContext) sws_freeContext_{};

 private:
  static bool loadLibrary(QLibrary* library,
                          const QString& path,
                          const QString& binDir,
                          const QString& label,
                          QString* errorMessage) {
    if (path.isEmpty()) {
      *errorMessage = missingDllMessage(binDir, label);
      return false;
    }
    library->setFileName(path);
    if (!library->load()) {
      *errorMessage = "加载 FFmpeg " + label + " DLL 失败: " +
                      library->errorString();
      return false;
    }
    return true;
  }

  bool resolveAll(QString* errorMessage) {
#define RESOLVE_FFMPEG_SYMBOL(library, symbol)                                \
  do {                                                                        \
    symbol##_ = reinterpret_cast<decltype(symbol##_)>((library).resolve(#symbol)); \
    if (!symbol##_) {                                                         \
      *errorMessage = QString("FFmpeg DLL 缺少符号: ") + #symbol;            \
      return false;                                                           \
    }                                                                         \
  } while (false)

    RESOLVE_FFMPEG_SYMBOL(avutil_, av_malloc);
    RESOLVE_FFMPEG_SYMBOL(avutil_, av_free);
    RESOLVE_FFMPEG_SYMBOL(avutil_, av_strerror);
    RESOLVE_FFMPEG_SYMBOL(avutil_, av_frame_alloc);
    RESOLVE_FFMPEG_SYMBOL(avutil_, av_frame_free);

    RESOLVE_FFMPEG_SYMBOL(avformat_, avio_alloc_context);
    RESOLVE_FFMPEG_SYMBOL(avformat_, avio_context_free);
    RESOLVE_FFMPEG_SYMBOL(avformat_, avformat_alloc_context);
    RESOLVE_FFMPEG_SYMBOL(avformat_, avformat_open_input);
    RESOLVE_FFMPEG_SYMBOL(avformat_, avformat_find_stream_info);
    RESOLVE_FFMPEG_SYMBOL(avformat_, av_read_frame);
    RESOLVE_FFMPEG_SYMBOL(avformat_, avformat_close_input);

    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_find_decoder);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_alloc_context3);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_parameters_to_context);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_open2);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_send_packet);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_receive_frame);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, avcodec_free_context);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, av_packet_alloc);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, av_packet_unref);
    RESOLVE_FFMPEG_SYMBOL(avcodec_, av_packet_free);

    RESOLVE_FFMPEG_SYMBOL(swscale_, sws_getContext);
    RESOLVE_FFMPEG_SYMBOL(swscale_, sws_scale);
    RESOLVE_FFMPEG_SYMBOL(swscale_, sws_freeContext);

#undef RESOLVE_FFMPEG_SYMBOL
    return true;
  }

  QMutex mutex_;
  QLibrary avutil_;
  QLibrary swscale_;
  QLibrary avcodec_;
  QLibrary avformat_;
  bool attempted_{};
  bool loaded_{};
  QString loadError_;
};

FfmpegApi& ffmpegApi() {
  static FfmpegApi instance;
  return instance;
}

ImageDecodeResult makeFailure(const QString& message) {
  return {false, {}, message, "FFmpeg DLL"};
}

class FfmpegInput {
 public:
  FfmpegInput(FfmpegApi* api, const QByteArray& data) : api_(api) {
    reader_.data = reinterpret_cast<const unsigned char*>(data.constData());
    reader_.size = data.size();
  }

  ~FfmpegInput() { close(); }

  FfmpegInput(const FfmpegInput&) = delete;
  FfmpegInput& operator=(const FfmpegInput&) = delete;

  bool open(QString* errorMessage) {
    constexpr int avioBufferSize = 32768;
    auto* avioBuffer =
        static_cast<unsigned char*>(api_->av_malloc_(avioBufferSize));
    if (!avioBuffer) {
      setError(errorMessage, "FFmpeg 分配 AVIO 缓冲区失败。");
      return false;
    }

    avioContext_ = api_->avio_alloc_context_(
        avioBuffer, avioBufferSize, 0, &reader_, readPacket, nullptr,
        seekPacket);
    if (!avioContext_) {
      api_->av_free_(avioBuffer);
      setError(errorMessage, "FFmpeg 创建 AVIOContext 失败。");
      return false;
    }

    formatContext_ = api_->avformat_alloc_context_();
    if (!formatContext_) {
      close();
      setError(errorMessage, "FFmpeg 创建 AVFormatContext 失败。");
      return false;
    }
    formatContext_->pb = avioContext_;
    formatContext_->flags |= AVFMT_FLAG_CUSTOM_IO;

    int ret =
        api_->avformat_open_input_(&formatContext_, nullptr, nullptr, nullptr);
    if (ret < 0) {
      const QString error = api_->avError(ret);
      close();
      setError(errorMessage, "FFmpeg 打开内存图片失败: " + error);
      return false;
    }

    ret = api_->avformat_find_stream_info_(formatContext_, nullptr);
    if (ret < 0) {
      const QString error = api_->avError(ret);
      close();
      setError(errorMessage, "FFmpeg 读取流信息失败: " + error);
      return false;
    }
    return true;
  }

  AVFormatContext* formatContext() const { return formatContext_; }

 private:
  static void setError(QString* errorMessage, const QString& message) {
    if (errorMessage) {
      *errorMessage = message;
    }
  }

  void close() {
    if (formatContext_) {
      formatContext_->pb = nullptr;
      api_->avformat_close_input_(&formatContext_);
    }
    if (avioContext_) {
      api_->avio_context_free_(&avioContext_);
    }
  }

  FfmpegApi* api_{};
  MemoryReader reader_;
  AVIOContext* avioContext_{};
  AVFormatContext* formatContext_{};
};

std::vector<StreamCandidate> collectVideoStreams(AVFormatContext* formatContext) {
  std::vector<StreamCandidate> candidates;
  for (unsigned int index = 0; index < formatContext->nb_streams; ++index) {
    AVStream* stream = formatContext->streams[index];
    if (!stream || !stream->codecpar ||
        stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO ||
        stream->codecpar->codec_id == AV_CODEC_ID_NONE) {
      continue;
    }
    const int width = qMax(stream->codecpar->width, 0);
    const int height = qMax(stream->codecpar->height, 0);
    candidates.push_back({static_cast<int>(index), width, height,
                          static_cast<qint64>(width) * height});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const StreamCandidate& left, const StreamCandidate& right) {
              if (left.area != right.area) {
                return left.area > right.area;
              }
              return left.index < right.index;
            });
  return candidates;
}

ImageDecodeResult decodeStreamCandidate(FfmpegApi& api,
                                        const QByteArray& data,
                                        const StreamCandidate& candidate) {
  QString openError;
  FfmpegInput input(&api, data);
  if (!input.open(&openError)) {
    return makeFailure(openError);
  }

  AVFormatContext* formatContext = input.formatContext();
  if (candidate.index < 0 ||
      candidate.index >= static_cast<int>(formatContext->nb_streams)) {
    return makeFailure("FFmpeg 图像流索引无效。");
  }

  AVStream* stream = formatContext->streams[candidate.index];
  const AVCodec* decoder =
      api.avcodec_find_decoder_(stream->codecpar->codec_id);
  if (!decoder) {
    return makeFailure("FFmpeg 未找到对应解码器。");
  }

  AVCodecContext* codecContext = api.avcodec_alloc_context3_(decoder);
  if (!codecContext) {
    return makeFailure("FFmpeg 创建解码上下文失败。");
  }

  int ret = api.avcodec_parameters_to_context_(codecContext, stream->codecpar);
  if (ret < 0) {
    const QString error = api.avError(ret);
    api.avcodec_free_context_(&codecContext);
    return makeFailure("FFmpeg 复制解码参数失败: " + error);
  }

  ret = api.avcodec_open2_(codecContext, decoder, nullptr);
  if (ret < 0) {
    const QString error = api.avError(ret);
    api.avcodec_free_context_(&codecContext);
    return makeFailure("FFmpeg 打开解码器失败: " + error);
  }

  AVPacket* packet = api.av_packet_alloc_();
  AVFrame* frame = api.av_frame_alloc_();
  if (!packet || !frame) {
    if (packet) {
      api.av_packet_free_(&packet);
    }
    if (frame) {
      api.av_frame_free_(&frame);
    }
    api.avcodec_free_context_(&codecContext);
    return makeFailure("FFmpeg 分配包或帧失败。");
  }

  bool gotFrame = false;
  QString decodeError;
  while (!gotFrame && (ret = api.av_read_frame_(formatContext, packet)) >= 0) {
    if (packet->stream_index == candidate.index) {
      const int sendRet = api.avcodec_send_packet_(codecContext, packet);
      if (sendRet < 0 && sendRet != AVERROR(EAGAIN)) {
        decodeError = "FFmpeg 发送压缩包失败: " + api.avError(sendRet);
        api.av_packet_unref_(packet);
        break;
      }

      while (true) {
        const int receiveRet = api.avcodec_receive_frame_(codecContext, frame);
        if (receiveRet == 0) {
          gotFrame = true;
          break;
        }
        if (receiveRet == AVERROR(EAGAIN) || receiveRet == AVERROR_EOF) {
          break;
        }
        decodeError = "FFmpeg 解码帧失败: " + api.avError(receiveRet);
        break;
      }
    }
    api.av_packet_unref_(packet);
  }

  if (!gotFrame && decodeError.isEmpty()) {
    api.avcodec_send_packet_(codecContext, nullptr);
    while (true) {
      const int receiveRet = api.avcodec_receive_frame_(codecContext, frame);
      if (receiveRet == 0) {
        gotFrame = true;
        break;
      }
      if (receiveRet == AVERROR(EAGAIN) || receiveRet == AVERROR_EOF) {
        break;
      }
      decodeError = "FFmpeg 刷新解码器失败: " + api.avError(receiveRet);
      break;
    }
  }

  ImageDecodeResult result;
  result.decoderName = "FFmpeg DLL";

  if (gotFrame) {
    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    if (image.isNull()) {
      result.message = "FFmpeg 解码成功，但创建 Qt 图像缓冲区失败。";
    } else {
      SwsContext* swsContext = api.sws_getContext_(
          frame->width, frame->height,
          static_cast<AVPixelFormat>(frame->format), frame->width,
          frame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
          nullptr);
      if (!swsContext) {
        result.message = "FFmpeg 创建像素格式转换器失败。";
      } else {
        unsigned char* dstData[4] = {image.bits(), nullptr, nullptr, nullptr};
        int dstLinesize[4] = {image.bytesPerLine(), 0, 0, 0};
        api.sws_scale_(swsContext, frame->data, frame->linesize, 0,
                       frame->height, dstData, dstLinesize);
        api.sws_freeContext_(swsContext);
        result.ok = true;
        result.image = image;
        result.message = "FFmpeg DLL 内存解码";
      }
    }
  } else {
    result.message =
        decodeError.isEmpty() ? "FFmpeg 未能从文件中解出图像帧。" : decodeError;
  }

  api.av_frame_free_(&frame);
  api.av_packet_free_(&packet);
  api.avcodec_free_context_(&codecContext);
  return result;
}

ImageDecodeResult decodeTileGridGroups(FfmpegApi& api,
                                       const QByteArray& data,
                                       QStringList* failures) {
  QString openError;
  FfmpegInput input(&api, data);
  if (!input.open(&openError)) {
    if (failures) {
      failures->push_back(openError);
    }
    return makeFailure(openError);
  }

  AVFormatContext* formatContext = input.formatContext();
  ImageDecodeResult bestResult;

  for (unsigned int groupIndex = 0; groupIndex < formatContext->nb_stream_groups;
       ++groupIndex) {
    AVStreamGroup* streamGroup = formatContext->stream_groups[groupIndex];
    if (!streamGroup ||
        streamGroup->type != AV_STREAM_GROUP_PARAMS_TILE_GRID ||
        !streamGroup->params.tile_grid) {
      continue;
    }

    const AVStreamGroupTileGrid* grid = streamGroup->params.tile_grid;
    if (grid->coded_width <= 0 || grid->coded_height <= 0 ||
        grid->width <= 0 || grid->height <= 0 || !grid->offsets ||
        grid->nb_tiles == 0) {
      if (failures) {
        failures->push_back(
            QString("tile grid %1 参数无效").arg(streamGroup->index));
      }
      continue;
    }

    QImage canvas(grid->coded_width, grid->coded_height,
                  QImage::Format_RGBA8888);
    if (canvas.isNull()) {
      if (failures) {
        failures->push_back(
            QString("tile grid %1 创建画布失败: %2x%3")
                .arg(streamGroup->index)
                .arg(grid->coded_width)
                .arg(grid->coded_height));
      }
      continue;
    }
    canvas.fill(QColor(grid->background[0], grid->background[1],
                       grid->background[2], grid->background[3]));

    QPainter painter(&canvas);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    bool complete = true;
    QStringList tileFailures;
    for (unsigned int tileIndex = 0; tileIndex < grid->nb_tiles; ++tileIndex) {
      const auto& offset = grid->offsets[tileIndex];
      if (offset.idx >= streamGroup->nb_streams ||
          !streamGroup->streams[offset.idx]) {
        complete = false;
        tileFailures.push_back(QString("tile %1 stream 索引无效")
                                   .arg(tileIndex));
        continue;
      }

      const int streamIndex =
          static_cast<int>(streamGroup->streams[offset.idx]->index);
      ImageDecodeResult tile =
          decodeStreamCandidate(api, data, {streamIndex, 0, 0, 0});
      if (!tile.ok) {
        complete = false;
        tileFailures.push_back(QString("tile %1(stream %2): %3")
                                   .arg(tileIndex)
                                   .arg(streamIndex)
                                   .arg(tile.message));
        continue;
      }
      painter.drawImage(offset.horizontal, offset.vertical, tile.image);
    }
    painter.end();

    if (!complete) {
      if (failures) {
        failures->push_back(QString("tile grid %1 拼接不完整: %2")
                                .arg(streamGroup->index)
                                .arg(tileFailures.join("；")));
      }
      continue;
    }

    const int cropX = qBound(0, grid->horizontal_offset, grid->coded_width - 1);
    const int cropY = qBound(0, grid->vertical_offset, grid->coded_height - 1);
    const int cropWidth =
        qBound(1, grid->width, grid->coded_width - cropX);
    const int cropHeight =
        qBound(1, grid->height, grid->coded_height - cropY);

    ImageDecodeResult result;
    result.ok = true;
    result.image = canvas.copy(cropX, cropY, cropWidth, cropHeight);
    result.decoderName =
        QString("FFmpeg DLL tile grid %1 (%2x%3)")
            .arg(streamGroup->index)
            .arg(result.image.width())
            .arg(result.image.height());
    result.message = "FFmpeg DLL tile grid 内存拼接";

    if (!bestResult.ok ||
        static_cast<qint64>(result.image.width()) * result.image.height() >
            static_cast<qint64>(bestResult.image.width()) *
                bestResult.image.height()) {
      bestResult = result;
    }
  }

  return bestResult.ok ? bestResult
                       : makeFailure("FFmpeg 未找到可用 tile grid。");
}

ImageDecodeResult decodeWithFfmpegDll(const QByteArray& data,
                                      const QString& qtError,
                                      const QString& imageMagickError) {
  FfmpegApi& api = ffmpegApi();
  QString loadError;
  if (!api.ensureLoaded(&loadError)) {
    return makeFailure("Qt 和 MagickWand 都无法解码该图片，且 FFmpeg DLL "
                       "不可用: " +
                       loadError + "。Qt 错误: " + qtError +
                       "。MagickWand 错误: " + imageMagickError);
  }

  if (data.isEmpty()) {
    return makeFailure("图片数据为空，无法交给 FFmpeg 解码。");
  }

  QStringList tileGridFailures;
  ImageDecodeResult tileGridResult =
      decodeTileGridGroups(api, data, &tileGridFailures);
  if (tileGridResult.ok) {
    return tileGridResult;
  }

  QString openError;
  FfmpegInput probeInput(&api, data);
  if (!probeInput.open(&openError)) {
    return makeFailure(openError);
  }

  const std::vector<StreamCandidate> candidates =
      collectVideoStreams(probeInput.formatContext());
  if (candidates.empty()) {
    return makeFailure("FFmpeg 未找到可解码的图像流。");
  }

  ImageDecodeResult bestResult;
  QStringList failures;
  for (const StreamCandidate& candidate : candidates) {
    ImageDecodeResult result = decodeStreamCandidate(api, data, candidate);
    if (!result.ok) {
      failures.push_back(QString("stream %1: %2")
                             .arg(candidate.index)
                             .arg(result.message));
      continue;
    }

    result.decoderName =
        QString("FFmpeg DLL stream %1 (%2x%3)")
            .arg(candidate.index)
            .arg(result.image.width())
            .arg(result.image.height());
    if (!bestResult.ok ||
        static_cast<qint64>(result.image.width()) * result.image.height() >
            static_cast<qint64>(bestResult.image.width()) *
                bestResult.image.height()) {
      bestResult = result;
    }

    const qint64 decodedArea =
        static_cast<qint64>(result.image.width()) * result.image.height();
    if (decodedArea >= candidate.area && candidate.area > 0) {
      break;
    }
  }

  if (bestResult.ok) {
    return bestResult;
  }
  QString message = "FFmpeg 未能从任一图像流解出帧: " + failures.join("；");
  if (!tileGridFailures.isEmpty()) {
    message += "。tile grid 尝试: " + tileGridFailures.join("；");
  }
  return makeFailure(message);
}

#endif

}  // namespace

ImageDecodeResult FfmpegImageDecoder::decodeToImage(
    const QByteArray& data,
    const QString& sourceName,
    const QString& qtError,
    const QString& imageMagickError) {
  Q_UNUSED(sourceName);
#if OPENLIST_SORTER_HAS_FFMPEG_DLL
  return decodeWithFfmpegDll(data, qtError, imageMagickError);
#else
  Q_UNUSED(data);
  return {false,
          {},
          "Qt 和 MagickWand 都无法解码该图片，且构建时未启用 FFmpeg DLL "
          "支持。请补齐 third_party/runtime/ffmpeg/bin 与 include 后重新配置。"
          "Qt 错误: " +
              qtError + "。MagickWand 错误: " + imageMagickError,
          "FFmpeg DLL"};
#endif
}
