#include "FFWaveDecoder.h"

#include "BinaryStream.h"
#include "StorageIntf.h"
#include "WaveIntf.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

int av_read(void* opaque, std::uint8_t* buffer, int size) {
    auto* stream = static_cast<TJS::tTJSBinaryStream*>(opaque);
    const auto read = stream->Read(buffer, static_cast<tjs_uint>(size));
    return read == 0 ? AVERROR_EOF : static_cast<int>(read);
}

std::int64_t av_seek(void* opaque, std::int64_t offset, int whence) {
    auto* stream = static_cast<TJS::tTJSBinaryStream*>(opaque);
    if (whence == AVSEEK_SIZE) return static_cast<std::int64_t>(stream->GetSize());
    return static_cast<std::int64_t>(stream->Seek(offset, whence & 0xff));
}

class VitaFFWaveDecoder final : public tTVPWaveDecoder {
public:
    ~VitaFFWaveDecoder() override { clear(); }

    bool open(const ttstr& storage_name) {
        clear();
        input_ = TVPCreateBinaryStreamForRead(storage_name, TJS_W(""));
        if (!input_) return false;

        constexpr int io_buffer_size = 32 * 1024;
        auto* io_buffer = static_cast<unsigned char*>(av_malloc(io_buffer_size));
        if (!io_buffer) return false;
        io_ = avio_alloc_context(io_buffer, io_buffer_size, 0, input_, av_read,
                                 nullptr, av_seek);
        if (!io_) {
            av_free(io_buffer);
            return false;
        }

        format_context_ = avformat_alloc_context();
        if (!format_context_) return false;
        format_context_->pb = io_;
        format_context_->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (avformat_open_input(&format_context_, nullptr, nullptr, nullptr) < 0)
            return false;
        if (avformat_find_stream_info(format_context_, nullptr) < 0) return false;

        stream_index_ = av_find_best_stream(format_context_, AVMEDIA_TYPE_AUDIO,
                                            -1, -1, &codec_, 0);
        if (stream_index_ < 0 || !codec_) return false;
        stream_ = format_context_->streams[stream_index_];

        codec_context_ = avcodec_alloc_context3(codec_);
        if (!codec_context_) return false;
        if (avcodec_parameters_to_context(codec_context_, stream_->codecpar) < 0)
            return false;
        codec_context_->pkt_timebase = stream_->time_base;
        if (avcodec_open2(codec_context_, codec_, nullptr) < 0) return false;

        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        if (!frame_ || !packet_) return false;

        channels_ = codec_context_->ch_layout.nb_channels;
        sample_format_ = codec_context_->sample_fmt;
        if (channels_ <= 0 || !supported_format(sample_format_)) return false;

        wave_format_.SamplesPerSec = codec_context_->sample_rate;
        wave_format_.Channels = static_cast<tjs_uint>(channels_);
        wave_format_.BitsPerSample =
            static_cast<tjs_uint>(av_get_bytes_per_sample(sample_format_) * 8);
        wave_format_.BytesPerSample = wave_format_.BitsPerSample / 8;
        wave_format_.IsFloat = sample_format_ == AV_SAMPLE_FMT_FLT ||
                               sample_format_ == AV_SAMPLE_FMT_FLTP;
        wave_format_.SpeakerConfig = 0;
        wave_format_.Seekable = format_context_->pb &&
                                (format_context_->pb->seekable & AVIO_SEEKABLE_NORMAL);

        if (stream_->duration != AV_NOPTS_VALUE) {
            const double seconds = stream_->duration * av_q2d(stream_->time_base);
            wave_format_.TotalSamples = static_cast<tjs_uint64>(
                std::max(0.0, seconds * wave_format_.SamplesPerSec));
            wave_format_.TotalTime =
                static_cast<tjs_uint64>(std::max(0.0, seconds * 1000.0));
        } else {
            wave_format_.TotalSamples = 0;
            wave_format_.TotalTime = 0;
        }
        return true;
    }

    void GetFormat(tTVPWaveFormat& format) override { format = wave_format_; }

    bool Render(void* output, tjs_uint requested, tjs_uint& rendered) override {
        rendered = 0;
        if (!codec_context_ || !output) return false;

        auto* destination = static_cast<unsigned char*>(output);
        const int bytes_per_sample = av_get_bytes_per_sample(sample_format_);
        const int bytes_per_frame = bytes_per_sample * channels_;
        while (rendered < requested) {
            if (frame_sample_ >= frame_->nb_samples) {
                if (!decode_frame()) break;
                frame_sample_ = 0;
            }
            const int available = frame_->nb_samples - frame_sample_;
            const int count = std::min<int>(available, requested - rendered);
            copy_samples(destination + rendered * bytes_per_frame, count,
                         frame_sample_, bytes_per_sample);
            frame_sample_ += count;
            rendered += count;
        }
        return rendered == requested || !decoder_eof_;
    }

    bool SetPosition(tjs_uint64 sample_position) override {
        if (!format_context_ || !stream_ || !wave_format_.Seekable) return false;
        const std::int64_t timestamp = av_rescale_q(
            static_cast<std::int64_t>(sample_position),
            AVRational{1, static_cast<int>(wave_format_.SamplesPerSec)},
            stream_->time_base);
        if (avformat_seek_file(format_context_, stream_index_, INT64_MIN,
                               timestamp, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
            return false;
        avcodec_flush_buffers(codec_context_);
        av_packet_unref(packet_);
        av_frame_unref(frame_);
        frame_sample_ = 0;
        input_eof_ = false;
        drain_sent_ = false;
        decoder_eof_ = false;
        return true;
    }

private:
    static bool supported_format(AVSampleFormat format) {
        switch (format) {
            case AV_SAMPLE_FMT_S16:
            case AV_SAMPLE_FMT_S16P:
            case AV_SAMPLE_FMT_S32:
            case AV_SAMPLE_FMT_S32P:
            case AV_SAMPLE_FMT_FLT:
            case AV_SAMPLE_FMT_FLTP:
                return true;
            default:
                return false;
        }
    }

    void clear() {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_context_);
        if (format_context_) avformat_close_input(&format_context_);
        if (io_) {
            av_freep(&io_->buffer);
            avio_context_free(&io_);
        }
        delete input_;
        input_ = nullptr;
        stream_ = nullptr;
        codec_ = nullptr;
    }

    bool decode_frame() {
        av_frame_unref(frame_);
        for (;;) {
            const int receive = avcodec_receive_frame(codec_context_, frame_);
            if (receive == 0) {
                if (!supported_format(static_cast<AVSampleFormat>(frame_->format)) ||
                    frame_->ch_layout.nb_channels != channels_)
                    return false;
                sample_format_ = static_cast<AVSampleFormat>(frame_->format);
                return true;
            }
            if (receive == AVERROR_EOF) {
                decoder_eof_ = true;
                return false;
            }
            if (receive != AVERROR(EAGAIN)) return false;

            if (input_eof_) {
                if (drain_sent_) {
                    decoder_eof_ = true;
                    return false;
                }
                const int send = avcodec_send_packet(codec_context_, nullptr);
                drain_sent_ = true;
                if (send < 0 && send != AVERROR_EOF) return false;
                continue;
            }

            int read_result;
            do {
                av_packet_unref(packet_);
                read_result = av_read_frame(format_context_, packet_);
            } while (read_result >= 0 && packet_->stream_index != stream_index_);
            if (read_result < 0) {
                input_eof_ = true;
                continue;
            }

            const int send = avcodec_send_packet(codec_context_, packet_);
            av_packet_unref(packet_);
            if (send < 0 && send != AVERROR(EAGAIN)) return false;
        }
    }

    void copy_samples(unsigned char* destination, int samples, int start,
                      int bytes_per_sample) const {
        if (!av_sample_fmt_is_planar(sample_format_) || channels_ == 1) {
            std::memcpy(destination,
                        frame_->extended_data[0] +
                            static_cast<std::size_t>(start) * bytes_per_sample *
                                channels_,
                        static_cast<std::size_t>(samples) * bytes_per_sample *
                            channels_);
            return;
        }

        for (int sample = 0; sample < samples; ++sample) {
            for (int channel = 0; channel < channels_; ++channel) {
                const auto* source = frame_->extended_data[channel] +
                    static_cast<std::size_t>(start + sample) * bytes_per_sample;
                std::memcpy(destination, source, bytes_per_sample);
                destination += bytes_per_sample;
            }
        }
    }

    tTJSBinaryStream* input_ = nullptr;
    AVIOContext* io_ = nullptr;
    AVFormatContext* format_context_ = nullptr;
    AVCodecContext* codec_context_ = nullptr;
    const AVCodec* codec_ = nullptr;
    AVStream* stream_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    tTVPWaveFormat wave_format_{};
    AVSampleFormat sample_format_ = AV_SAMPLE_FMT_NONE;
    int stream_index_ = -1;
    int channels_ = 0;
    int frame_sample_ = 0;
    bool input_eof_ = false;
    bool drain_sent_ = false;
    bool decoder_eof_ = false;
};

} // namespace

void TVPInitLibAVCodec() {
    // Current FFmpeg performs codec/demuxer registration internally.
    static const int network_init = avformat_network_init();
    (void)network_init;
}

tTVPWaveDecoder* FFWaveDecoderCreator::Create(const ttstr& storage_name,
                                               const ttstr&) {
    TVPInitLibAVCodec();
    auto* decoder = new VitaFFWaveDecoder();
    if (!decoder->open(storage_name)) {
        delete decoder;
        return nullptr;
    }
    return decoder;
}
