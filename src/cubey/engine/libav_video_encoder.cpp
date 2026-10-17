#include <cubey/engine/video_encoder.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace cubey {
namespace {

[[nodiscard]] std::string av_error_message(int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(error, buffer, sizeof(buffer));
    return buffer;
}

void check_av(int status, std::string_view action) {
    if (status < 0) {
        throw std::runtime_error(std::string(action) + ": " + av_error_message(status));
    }
}

[[nodiscard]] const AVCodec* find_h264_encoder() {
    if (const AVCodec* codec = avcodec_find_encoder_by_name("libx264")) {
        return codec;
    }
    return avcodec_find_encoder(AV_CODEC_ID_H264);
}

[[nodiscard]] int checked_int(std::uint32_t value, const char* name) {
    if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error(std::string("video ") + name + " exceeds libav int range");
    }
    return static_cast<int>(value);
}

class LibavVideoEncoder final : public VideoEncoder {
    struct Resources {
        AVFormatContext* format_ = nullptr;
        AVCodecContext* codec_context_ = nullptr;
        AVStream* stream_ = nullptr;
        AVFrame* frame_ = nullptr;
        AVPacket* packet_ = nullptr;
        SwsContext* scaler_ = nullptr;

        ~Resources() {
            av_packet_free(&packet_);
            av_frame_free(&frame_);
            sws_freeContext(scaler_);
            scaler_ = nullptr;
            avcodec_free_context(&codec_context_);
            if (format_ != nullptr) {
                if (format_->pb != nullptr) {
                    static_cast<void>(avio_closep(&format_->pb));
                }
                avformat_free_context(format_);
                format_ = nullptr;
            }
        }
    };

  public:
    explicit LibavVideoEncoder(VideoEncoderConfig config) : config_(std::move(config)) {
        av_log_set_level(AV_LOG_ERROR);
        validate_video_encoder_config(config_);
        if ((config_.width % 2U) != 0 || (config_.height % 2U) != 0) {
            throw std::runtime_error("libav H.264 video capture requires even dimensions");
        }

        const AVCodec* codec = find_h264_encoder();
        if (codec == nullptr) {
            throw std::runtime_error("libav H.264 encoder is not available");
        }

        const std::string output_path = config_.output_path.string();
        check_av(avformat_alloc_output_context2(&resources_.format_, nullptr, nullptr,
                                                output_path.c_str()),
                 "create video output context");
        if (resources_.format_ == nullptr) {
            throw std::runtime_error("create video output context: unsupported output format");
        }

        resources_.stream_ = avformat_new_stream(resources_.format_, nullptr);
        if (resources_.stream_ == nullptr) {
            throw std::runtime_error("create video stream failed");
        }

        resources_.codec_context_ = avcodec_alloc_context3(codec);
        if (resources_.codec_context_ == nullptr) {
            throw std::runtime_error("allocate video codec context failed");
        }

        const int width = checked_int(config_.width, "width");
        const int height = checked_int(config_.height, "height");
        const int fps = checked_int(config_.fps, "fps");
        resources_.codec_context_->codec_id = codec->id;
        resources_.codec_context_->codec_type = AVMEDIA_TYPE_VIDEO;
        resources_.codec_context_->width = width;
        resources_.codec_context_->height = height;
        resources_.codec_context_->time_base = AVRational{1, fps};
        resources_.codec_context_->framerate = AVRational{fps, 1};
        resources_.codec_context_->pix_fmt = AV_PIX_FMT_YUV420P;
        resources_.codec_context_->gop_size = static_cast<int>(
            std::clamp<std::uint64_t>(static_cast<std::uint64_t>(config_.fps) * 2U, 12U, 240U));
        resources_.codec_context_->max_b_frames = 0;

        if ((resources_.format_->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
            resources_.codec_context_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        if (codec->id == AV_CODEC_ID_H264 && resources_.codec_context_->priv_data != nullptr) {
            static_cast<void>(
                av_opt_set(resources_.codec_context_->priv_data, "preset", "veryfast", 0));
            static_cast<void>(av_opt_set(resources_.codec_context_->priv_data, "crf", "18", 0));
        }

        check_av(avcodec_open2(resources_.codec_context_, codec, nullptr), "open video encoder");
        check_av(avcodec_parameters_from_context(resources_.stream_->codecpar,
                                                 resources_.codec_context_),
                 "copy video stream parameters");
        resources_.stream_->time_base = resources_.codec_context_->time_base;

        if ((resources_.format_->oformat->flags & AVFMT_NOFILE) == 0) {
            check_av(avio_open(&resources_.format_->pb, output_path.c_str(), AVIO_FLAG_WRITE),
                     "open video output file");
        }
        check_av(avformat_write_header(resources_.format_, nullptr), "write video header");
        resources_.frame_ = av_frame_alloc();
        if (resources_.frame_ == nullptr) {
            throw std::runtime_error("allocate video frame failed");
        }
        resources_.frame_->format = resources_.codec_context_->pix_fmt;
        resources_.frame_->width = resources_.codec_context_->width;
        resources_.frame_->height = resources_.codec_context_->height;
        check_av(av_frame_get_buffer(resources_.frame_, 32), "allocate video frame buffers");

        resources_.packet_ = av_packet_alloc();
        if (resources_.packet_ == nullptr) {
            throw std::runtime_error("allocate video packet failed");
        }

        resources_.scaler_ = sws_getContext(width, height, AV_PIX_FMT_RGBA, width, height,
                                            resources_.codec_context_->pix_fmt, SWS_BICUBIC,
                                            nullptr, nullptr, nullptr);
        if (resources_.scaler_ == nullptr) {
            throw std::runtime_error("create RGBA to YUV scaler failed");
        }
    }

    ~LibavVideoEncoder() override {
        if (!finished_) {
            try {
                finish();
            } catch (...) {
            }
        }
    }

    void write_frame(std::span<const std::uint8_t> rgba8) override {
        if (finished_) {
            throw std::runtime_error("cannot write a video frame after finish");
        }
        validate_video_frame_size(config_.width, config_.height, rgba8.size());
        check_av(av_frame_make_writable(resources_.frame_), "make video frame writable");

        const std::uint8_t* source_data[4] = {rgba8.data(), nullptr, nullptr, nullptr};
        const int source_linesize[4] = {
            checked_int(config_.width, "width") * static_cast<int>(sizeof(std::uint32_t)), 0, 0, 0};
        const int scaled_rows = sws_scale(resources_.scaler_, source_data, source_linesize, 0,
                                          resources_.codec_context_->height,
                                          resources_.frame_->data, resources_.frame_->linesize);
        if (scaled_rows != resources_.codec_context_->height) {
            throw std::runtime_error("scale RGBA frame for video failed");
        }

        resources_.frame_->pts = next_pts_++;
        encode(resources_.frame_);
    }

    void finish() override {
        if (finished_) {
            return;
        }
        encode(nullptr);
        check_av(av_write_trailer(resources_.format_), "write video trailer");
        finished_ = true;
    }

  private:
    void encode(AVFrame* frame) {
        check_av(avcodec_send_frame(resources_.codec_context_, frame),
                 "send frame to video encoder");
        while (true) {
            const int status =
                avcodec_receive_packet(resources_.codec_context_, resources_.packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
                return;
            }
            check_av(status, "receive encoded video packet");
            // This encoder emits fixed-rate, single-frame H.264 packets. Some
            // libav versions leave duration unknown; the final MP4 sample then
            // falls outside the track duration/edit list during normal decode.
            if (resources_.packet_->duration == 0)
                resources_.packet_->duration = 1; // Codec time base is exactly 1 / fps.
            av_packet_rescale_ts(resources_.packet_, resources_.codec_context_->time_base,
                                 resources_.stream_->time_base);
            resources_.packet_->stream_index = resources_.stream_->index;
            check_av(av_interleaved_write_frame(resources_.format_, resources_.packet_),
                     "write encoded video packet");
            av_packet_unref(resources_.packet_);
        }
    }

    VideoEncoderConfig config_;
    Resources resources_;
    std::int64_t next_pts_ = 0;
    bool finished_ = false;
};

} // namespace

bool libav_video_encoder_available() {
    return find_h264_encoder() != nullptr;
}

const char* libav_video_encoder_backend_name() {
    return libav_video_encoder_available() ? "libav h264" : "libav unavailable";
}

std::unique_ptr<VideoEncoder> create_libav_video_encoder(VideoEncoderConfig config) {
    return std::make_unique<LibavVideoEncoder>(std::move(config));
}

} // namespace cubey
