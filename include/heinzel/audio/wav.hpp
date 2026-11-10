#pragma once

// heinzel::audio: an in-house WAV reader and writer with no dependency, always available. Host-only
// (<cstdio>), no exceptions: every fallible call returns bool or a count and exposes a const char* error().
// Decoded samples are interleaved float32, frame-major (dst[frame*channels + ch]); integer<->float scaling
// is symmetric and matches libsndfile (u8 (x-128)/128, s16 /32768, s24 /8388608, s32 /2147483648, float
// passed through).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace heinzel::audio {

// WAVE format tags we resolve to. EXTENSIBLE (0xFFFE) is resolved via the SubFormat GUID's first two
// bytes to PCM (1) or IEEE_FLOAT (3) at open().
inline constexpr std::uint16_t kFmtPcm = 1;
inline constexpr std::uint16_t kFmtFloat = 3;
inline constexpr std::uint16_t kFmtExtensible = 0xFFFE;

// The concrete raw sample encoding, selected once at open() so the read() conversion loop branches
// per-buffer, never per-sample.
enum class sample_kind { u8, s16, s24, s32, f32, f64 };

// ---- little-endian byte helpers --------------------------------------------------------------------
inline std::uint16_t rd_u16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
inline std::uint32_t rd_u32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
inline void wr_u16(unsigned char* p, std::uint16_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
}
inline void wr_u32(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v & 0xFF);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFF);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFF);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFF);
}
inline bool tag_is(const unsigned char* p, const char (&s)[5]) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

// ===================================================================================================
// Reader
// ===================================================================================================
struct wav_reader {
    bool open(const char* path) {
        close();
        f_ = std::fopen(path, "rb");
        if (!f_) {
            err_ = "wav: cannot open file";
            return false;
        }
        owns_ = true;
        // Seekable path → probe the total size so a bogus/streamed data size is detectable.
        if (std::fseek(f_, 0, SEEK_END) == 0) {
            long sz = std::ftell(f_);
            std::fseek(f_, 0, SEEK_SET);
            file_size_ = sz > 0 ? static_cast<std::int64_t>(sz) : -1;
        }
        return parse_headers();
    }

    // Non-owning; enables stdin. `pre`/`pre_n` = bytes the caller already consumed (a format sniff),
    // drained before reading from `f`, so pipes need no seek-back.
    bool open(std::FILE* f, const unsigned char* pre = nullptr, std::size_t pre_n = 0) {
        close();
        f_ = f;
        owns_ = false;
        pre_ = pre;
        pre_n_ = pre_n;
        pre_pos_ = 0;
        file_size_ = -1; // treat a handed-in stream as non-seekable for size purposes
        return parse_headers();
    }

    int sample_rate() const { return sample_rate_; }
    int channels() const { return channels_; }
    std::int64_t frames() const { return total_frames_; }

    std::size_t read(float* dst, std::size_t max_frames) {
        if (!f_ || err_ || channels_ <= 0) {
            return 0;
        }
        const std::size_t fb = static_cast<std::size_t>(frame_bytes_);
        std::size_t done = 0;
        while (done < max_frames) {
            std::size_t want_frames = max_frames - done;
            // Cap by the declared data remaining (unless unknown).
            if (data_remaining_ >= 0) {
                std::size_t avail = static_cast<std::size_t>(data_remaining_) / fb;
                if (avail == 0) {
                    break;
                }
                if (want_frames > avail) {
                    want_frames = avail;
                }
            }
            const std::size_t buf_frames = kBufBytes / fb; // at least one: `parse_fmt` refuses a larger frame
            if (want_frames > buf_frames) {
                want_frames = buf_frames;
            }
            const std::size_t want_bytes = want_frames * fb;
            const std::size_t got = read_raw(buf_, want_bytes);
            const std::size_t got_frames = got / fb;
            if (got_frames == 0) {
                break; // EOF (or partial frame at EOF — dropped)
            }
            convert(buf_, dst + done * static_cast<std::size_t>(channels_), got_frames);
            if (data_remaining_ >= 0) {
                data_remaining_ -= static_cast<std::int64_t>(got_frames) * static_cast<std::int64_t>(fb);
            }
            done += got_frames;
            if (got < want_bytes) {
                break; // short read = EOF
            }
        }
        return done;
    }

    void close() {
        if (f_ && owns_) {
            std::fclose(f_);
        }
        f_ = nullptr;
        owns_ = false;
        pre_ = nullptr;
        pre_n_ = pre_pos_ = 0;
        err_ = nullptr;
        sample_rate_ = channels_ = 0;
        frame_bytes_ = 0;
        total_frames_ = -1;
        data_remaining_ = -1;
        file_size_ = -1;
    }

    const char* error() const { return err_ ? err_ : ""; }

    ~wav_reader() { close(); }

  private:
    static constexpr std::size_t kBufBytes = 64 * 1024;

    std::size_t read_raw(void* dst, std::size_t n) {
        auto* out = static_cast<unsigned char*>(dst);
        std::size_t got = 0;
        while (pre_pos_ < pre_n_ && got < n) {
            out[got++] = pre_[pre_pos_++];
        }
        if (got < n) {
            got += std::fread(out + got, 1, n - got, f_);
        }
        return got;
    }

    // Read-and-discard n bytes (used to skip chunks we don't consume; works on pipes).
    bool skip_raw(std::uint64_t n) { // 64 bits: a chunk's size and its pad byte may pass 2^32 - 1
        unsigned char tmp[512];
        while (n > 0) {
            const std::size_t chunk = n < sizeof(tmp) ? static_cast<std::size_t>(n) : sizeof(tmp);
            if (read_raw(tmp, chunk) != chunk) {
                return false;
            }
            n -= chunk;
        }
        return true;
    }

    bool fail(const char* m) {
        err_ = m;
        return false;
    }

    bool parse_headers() {
        unsigned char hdr[12];
        if (read_raw(hdr, 12) != 12) {
            return fail("wav: file too short for a RIFF header");
        }
        if (tag_is(hdr, "RF64") || tag_is(hdr, "BW64")) {
            return fail("wav: RF64/BW64 (>4GB) not supported");
        }
        if (!tag_is(hdr, "RIFF") || !tag_is(hdr + 8, "WAVE")) {
            return fail("wav: not a RIFF/WAVE file");
        }
        std::int64_t consumed = 12;
        bool have_fmt = false;
        // Walk chunks until the data chunk. Chunk = 4-byte id + 4-byte LE size + payload (padded even).
        for (;;) {
            unsigned char ch[8];
            if (read_raw(ch, 8) != 8) {
                return fail("wav: no data chunk found before EOF");
            }
            consumed += 8;
            const std::uint32_t size = rd_u32(ch + 4);
            if (tag_is(ch, "fmt ")) {
                if (!parse_fmt(size)) {
                    return false;
                }
                have_fmt = true;
                consumed += static_cast<std::int64_t>(size) + (size & 1u);
            } else if (tag_is(ch, "data")) {
                if (!have_fmt) {
                    return fail("wav: data chunk precedes fmt chunk");
                }
                return finish_data(size, consumed);
            } else {
                if (!skip_raw(std::uint64_t{size} + (size & 1u))) {
                    return fail("wav: truncated inside a skipped chunk");
                }
                consumed += static_cast<std::int64_t>(size) + (size & 1u);
            }
        }
    }

    bool parse_fmt(std::uint32_t size) {
        if (size < 16 || size > 64) {
            return fail("wav: implausible fmt chunk size");
        }
        unsigned char fmt[64];
        if (read_raw(fmt, size) != size) {
            return fail("wav: truncated fmt chunk");
        }
        if (size & 1u) {
            unsigned char pad;
            read_raw(&pad, 1); // consume the pad byte
        }
        std::uint16_t tag = rd_u16(fmt);
        channels_ = rd_u16(fmt + 2);
        sample_rate_ = static_cast<int>(rd_u32(fmt + 4));
        const std::uint16_t bits = rd_u16(fmt + 14);
        if (tag == kFmtExtensible) {
            if (size < 40) {
                return fail("wav: EXTENSIBLE fmt chunk too small");
            }
            tag = rd_u16(fmt + 24); // SubFormat GUID's first two bytes carry the real tag
        }
        if (channels_ <= 0 || sample_rate_ <= 0) {
            return fail("wav: nonsensical channel count or sample rate");
        }
        if (tag == kFmtPcm) {
            switch (bits) {
                case 8: kind_ = sample_kind::u8; break;
                case 16: kind_ = sample_kind::s16; break;
                case 24: kind_ = sample_kind::s24; break;
                case 32: kind_ = sample_kind::s32; break;
                default: return fail("wav: unsupported PCM bit depth (8/16/24/32 only)");
            }
        } else if (tag == kFmtFloat) {
            switch (bits) {
                case 32: kind_ = sample_kind::f32; break;
                case 64: kind_ = sample_kind::f64; break;
                default: return fail("wav: unsupported float bit depth (32/64 only)");
            }
        } else {
            return fail("wav: unsupported format tag (only PCM and IEEE_FLOAT)");
        }
        frame_bytes_ = (static_cast<int>(bits) / 8) * channels_;
        if (frame_bytes_ <= 0) {
            return fail("wav: zero frame size");
        }
        if (static_cast<std::size_t>(frame_bytes_) > kBufBytes) { // `read` fills whole frames into `buf_`
            return fail("wav: a frame over 64 KiB (that many channels) is not read");
        }
        return true;
    }

    bool finish_data(std::uint32_t declared, std::int64_t data_start) {
        bool unknown = (declared == 0xFFFFFFFFu);
        if (!unknown && file_size_ > 0) {
            const std::int64_t avail = file_size_ - data_start;
            if (avail > 0 && static_cast<std::int64_t>(declared) > avail) {
                unknown = true; // declared size exceeds the file: a streamed/piped producer
            }
        }
        if (unknown) {
            data_remaining_ = -1;
            total_frames_ = -1;
        } else {
            data_remaining_ = static_cast<std::int64_t>(declared);
            total_frames_ = static_cast<std::int64_t>(declared) / frame_bytes_;
        }
        return true;
    }

    void convert(const unsigned char* src, float* dst, std::size_t frames) {
        const std::size_t n = frames * static_cast<std::size_t>(channels_);
        switch (kind_) {
            case sample_kind::u8:
                for (std::size_t i = 0; i < n; ++i) {
                    dst[i] = (static_cast<float>(src[i]) - 128.0f) / 128.0f;
                }
                break;
            case sample_kind::s16:
                for (std::size_t i = 0; i < n; ++i) {
                    const auto v = static_cast<std::int16_t>(rd_u16(src + i * 2));
                    dst[i] = static_cast<float>(v) / 32768.0f;
                }
                break;
            case sample_kind::s24:
                for (std::size_t i = 0; i < n; ++i) {
                    const unsigned char* p = src + i * 3;
                    std::int32_t v = p[0] | (p[1] << 8) | (p[2] << 16);
                    if (v & 0x800000) {
                        v |= ~0xFFFFFF; // sign-extend the 24-bit value
                    }
                    dst[i] = static_cast<float>(v) / 8388608.0f;
                }
                break;
            case sample_kind::s32:
                for (std::size_t i = 0; i < n; ++i) {
                    const auto v = static_cast<std::int32_t>(rd_u32(src + i * 4));
                    dst[i] = static_cast<float>(v) / 2147483648.0f;
                }
                break;
            case sample_kind::f32:
                for (std::size_t i = 0; i < n; ++i) {
                    std::uint32_t bits = rd_u32(src + i * 4);
                    float f;
                    std::memcpy(&f, &bits, 4);
                    dst[i] = f;
                }
                break;
            case sample_kind::f64:
                for (std::size_t i = 0; i < n; ++i) {
                    std::uint64_t lo = rd_u32(src + i * 8);
                    std::uint64_t hi = rd_u32(src + i * 8 + 4);
                    std::uint64_t bits = lo | (hi << 32);
                    double d;
                    std::memcpy(&d, &bits, 8);
                    dst[i] = static_cast<float>(d);
                }
                break;
        }
    }

    std::FILE* f_ = nullptr;
    bool owns_ = false;
    const unsigned char* pre_ = nullptr;
    std::size_t pre_n_ = 0;
    std::size_t pre_pos_ = 0;
    const char* err_ = nullptr;
    int sample_rate_ = 0;
    int channels_ = 0;
    int frame_bytes_ = 0;
    std::int64_t total_frames_ = -1;
    std::int64_t data_remaining_ = -1;
    std::int64_t file_size_ = -1;
    sample_kind kind_ = sample_kind::f32;
    unsigned char buf_[kBufBytes];
};

// ===================================================================================================
// Writer
// ===================================================================================================
enum class wav_format { f32, s16 };

struct wav_writer {
    bool open(const char* path, int sample_rate, int channels, wav_format fmt = wav_format::f32) {
        close();
        err_ = refusal(sample_rate, channels, fmt); // before the file is opened, and so emptied
        if (err_) {
            return false;
        }
        f_ = std::fopen(path, "wb");
        if (!f_) {
            err_ = "wav: cannot open output file";
            return false;
        }
        owns_ = true;
        return write_header(sample_rate, channels, fmt);
    }

    bool open(std::FILE* f, int sample_rate, int channels, wav_format fmt = wav_format::f32) {
        close();
        err_ = refusal(sample_rate, channels, fmt);
        if (err_) {
            return false;
        }
        f_ = f;
        owns_ = false;
        return write_header(sample_rate, channels, fmt);
    }

    // Interleaved float32 in. float32 output is verbatim (no clamp/dither); s16 clamps + round-nearest.
    bool write(const float* src, std::size_t frames) {
        if (!f_ || err_) {
            return false;
        }
        const std::size_t n = frames * static_cast<std::size_t>(channels_);
        if (fmt_ == wav_format::f32) {
            for (std::size_t off = 0; off < n;) {
                const std::size_t chunk = n - off < kBufSamples ? n - off : kBufSamples;
                for (std::size_t i = 0; i < chunk; ++i) {
                    std::uint32_t bits;
                    std::memcpy(&bits, &src[off + i], 4);
                    wr_u32(buf_ + i * 4, bits);
                }
                if (std::fwrite(buf_, 4, chunk, f_) != chunk) {
                    err_ = "wav: write failed";
                    return false;
                }
                off += chunk;
            }
            data_bytes_ += static_cast<std::int64_t>(n) * 4;
        } else {
            for (std::size_t off = 0; off < n;) {
                const std::size_t chunk = n - off < kBufSamples ? n - off : kBufSamples;
                for (std::size_t i = 0; i < chunk; ++i) {
                    float v = src[off + i] * 32768.0f;
                    if (v > 32767.0f) {
                        v = 32767.0f;
                    } else if (v < -32768.0f) {
                        v = -32768.0f;
                    }
                    wr_u16(buf_ + i * 2, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrintf(v))));
                }
                if (std::fwrite(buf_, 2, chunk, f_) != chunk) {
                    err_ = "wav: write failed";
                    return false;
                }
                off += chunk;
            }
            data_bytes_ += static_cast<std::int64_t>(n) * 2;
        }
        return true;
    }

    // Patch RIFF/data sizes in a file this writer opened, if it can be sought (a FIFO cannot); a stream
    // it was handed (stdout) keeps the 0xFFFFFFFF placeholders (the streaming-WAV convention our own reader
    // and most players accept) — where that stream's WAV starts, and whether a seek there would land
    // where it aims (`>>`), is not the writer's to know. False when anything written, patched or flushed
    // did not reach the file.
    bool close() {
        bool ok = true;
        if (f_) {
            ok = std::fflush(f_) == 0; // what was written reached the file, before any seek
            if (ok && owns_ && data_bytes_ >= 0 && data_bytes_ <= 0xFFFFFFFFll - 36 && std::fseek(f_, 4, SEEK_SET) == 0) {
                unsigned char b[4];
                wr_u32(b, static_cast<std::uint32_t>(36 + data_bytes_));
                ok = std::fwrite(b, 1, 4, f_) == 4;
                wr_u32(b, static_cast<std::uint32_t>(data_bytes_));
                ok = ok && std::fseek(f_, 40, SEEK_SET) == 0 && std::fwrite(b, 1, 4, f_) == 4;
            }
            ok = (owns_ ? std::fclose(f_) == 0 : std::fflush(f_) == 0) && ok;
            err_ = ok || err_ ? err_ : "wav: write failed";
        }
        f_ = nullptr;
        owns_ = false;
        data_bytes_ = 0;
        return ok && !err_;
    }

    const char* error() const { return err_ ? err_ : ""; }

    ~wav_writer() { close(); }

  private:
    static constexpr std::size_t kBufSamples = 16 * 1024;

    // Why a header cannot say this, or null: a frame's bytes are a 16-bit field, a second's a 32-bit one.
    static const char* refusal(int sample_rate, int channels, wav_format fmt) {
        const std::int64_t frame = static_cast<std::int64_t>(channels) * (fmt == wav_format::f32 ? 4 : 2);
        if (channels <= 0 || sample_rate <= 0) {
            return "wav: bad channel count or sample rate";
        }
        if (frame > 65535) {
            return "wav: too many channels for a WAV frame";
        }
        return frame * sample_rate > 0xFFFFFFFFll ? "wav: too many bytes a second for a WAV header" : nullptr;
    }

    bool write_header(int sample_rate, int channels, wav_format fmt) {
        channels_ = channels;
        fmt_ = fmt;
        const std::uint16_t bits = fmt == wav_format::f32 ? 32 : 16;
        const std::uint16_t tag = fmt == wav_format::f32 ? kFmtFloat : kFmtPcm;
        const std::uint16_t block_align = static_cast<std::uint16_t>(channels * (bits / 8));
        const std::uint32_t byte_rate = static_cast<std::uint32_t>(sample_rate) * block_align;
        unsigned char h[44];
        std::memcpy(h, "RIFF", 4);
        wr_u32(h + 4, 0xFFFFFFFFu); // riff size — patched on close if seekable
        std::memcpy(h + 8, "WAVE", 4);
        std::memcpy(h + 12, "fmt ", 4);
        wr_u32(h + 16, 16);
        wr_u16(h + 20, tag);
        wr_u16(h + 22, static_cast<std::uint16_t>(channels));
        wr_u32(h + 24, static_cast<std::uint32_t>(sample_rate));
        wr_u32(h + 28, byte_rate);
        wr_u16(h + 32, block_align);
        wr_u16(h + 34, bits);
        std::memcpy(h + 36, "data", 4);
        wr_u32(h + 40, 0xFFFFFFFFu); // data size — patched on close if seekable
        if (std::fwrite(h, 1, 44, f_) != 44) {
            err_ = "wav: header write failed";
            return false;
        }
        data_bytes_ = 0;
        return true;
    }

    std::FILE* f_ = nullptr;
    bool owns_ = false;
    const char* err_ = nullptr;
    int channels_ = 0;
    wav_format fmt_ = wav_format::f32;
    std::int64_t data_bytes_ = 0;
    unsigned char buf_[kBufSamples * 4];
};

} // namespace heinzel::audio
