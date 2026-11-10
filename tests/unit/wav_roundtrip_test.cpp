// The in-house WAV reader/writer round-trips a known buffer — MF.6 s1 of the multi-rate / frame-stream
// model's audio layer (Decision 44, representation A). The WAV I/O is the buildable slice of the old
// tree's `heinzel/audio/` (flac/mp3/opus decode stays a recorded reserved absence — external libs, no
// sudo). `examples/synth_voice.cpp` writes its file with it; here it is checked in isolation.
//
// Data convention (matches the header's doc and libsndfile): interleaved float32, frame-major
// (buf[frame*channels + ch]); integer<->float scaling symmetric (s16 /32768).
//
// Three things are asked, each the definition of "round-trips a known buffer":
//   1. float32 output is verbatim (the writer's own promise: no clamp, no dither) — read back
//      BIT-EXACT via memcmp, so a single flipped byte in the little-endian pack/unpack is caught.
//   2. the FORMAT survives — channels, sample rate and frame count read back equal to what was
//      written. The sample rate carried through the file IS the file-level half of "sample-rate bind":
//      a run binds its rate from the rate the reader reports here.
//   3. s16 output round-trips within one quantisation step (1/32768) — the lossy path, checked to its
//      own tolerance rather than asserted exact, and with at least one negative and one positive
//      sample so a sign-extension error in the 16-bit path shows.
//
// Then the behaviours the audio CLI's tests pinned until the CLI left the tree (Plan/archive/14-dsl DS-20), each
// through the entry point the CLI used (e6cdcbc7, 045f4829, ada58597, 1a2e9c99): a stream read through
// `open(FILE*)`, a streamed header's placeholders, the reader's two refusals of a hostile header, s16's
// rounding and clamp, a header too wide refused before the file is touched, the sizes patched in a file
// opened by path, a FIFO and a socket written whole, and a failed write reported. They read back in
// chunks smaller than the file, the last one partial, and one file is read past a chunk that follows
// its data, in one call longer than the reader's buffer and in chunks, so `read`'s bookkeeping shows.
// What goes through a pipe, a FIFO or a socket stays under 4 KiB, which the smallest pipe holds, so a
// case that blocks cannot hang; the `/dev/full` cases run where that device exists (Linux).
//
// No graph machinery: this exercises wav.hpp alone, so it depends on nothing downstream and fails for
// its own reason if the byte layout is wrong.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "heinzel/audio/wav.hpp"

namespace {

using heinzel::audio::wav_format;
using heinzel::audio::wav_reader;
using heinzel::audio::wav_writer;

int g_wrong = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        ++g_wrong;
        std::printf("  FAILED: %s\n", what);
    }
}

// One directory per run: two build trees may run this test at once.
const std::filesystem::path kDir = std::filesystem::temp_directory_path() / ("heinzel_wav_roundtrip_test_" + std::to_string(getpid()));
std::string at(const char* name) { return (kDir / name).string(); }

// A known stereo buffer: 6 frames, 2 channels, interleaved. Distinct per channel and per frame, and
// spanning both signs, so a swapped channel or a dropped/duplicated frame diverges. Every value is an
// exact binary fraction so the float32 path is genuinely bit-exact rather than accidentally so.
constexpr int kChannels = 2;
constexpr int kSampleRate = 44100;
constexpr std::size_t kFrames = 6;
constexpr float kKnown[kFrames * kChannels] = {
    0.0f,     -0.5f,   //
    0.25f,    -0.75f,  //
    0.5f,      0.125f, //
    -0.875f,   0.375f, //
    0.75f,    -0.25f,  //
    -0.125f,   0.625f, //
};

bool write_known(const char* path, wav_format fmt) {
    wav_writer w;
    if (!w.open(path, kSampleRate, kChannels, fmt)) {
        return false;
    }
    if (!w.write(kKnown, kFrames)) {
        return false;
    }
    return w.close(); // patches RIFF/data sizes on a seekable file
}

// Read the whole file back into `dst` (room for kFrames), returning the frame count read, or 0 on any
// mismatch of the reported format against what was written.
std::size_t read_all(const char* path, float* dst) {
    wav_reader r;
    if (!r.open(path)) {
        return 0;
    }
    if (r.channels() != kChannels || r.sample_rate() != kSampleRate) {
        return 0; // condition 2: the format (incl. the sample rate) must survive
    }
    if (r.frames() != static_cast<std::int64_t>(kFrames)) {
        return 0; // a seekable file reports its exact frame count
    }
    return r.read(dst, kFrames);
}

bool float32_is_bit_exact() {
    const std::string path = at("f32.wav");
    if (!write_known(path.c_str(), wav_format::f32)) {
        return false;
    }
    float back[kFrames * kChannels] = {};
    if (read_all(path.c_str(), back) != kFrames) {
        return false;
    }
    // Verbatim float32: the bytes must be identical, not merely close.
    return std::memcmp(kKnown, back, sizeof(kKnown)) == 0;
}

bool s16_round_trips_within_a_step() {
    const std::string path = at("s16.wav");
    if (!write_known(path.c_str(), wav_format::s16)) {
        return false;
    }
    float back[kFrames * kChannels] = {};
    if (read_all(path.c_str(), back) != kFrames) {
        return false;
    }
    bool saw_negative = false;
    bool saw_positive = false;
    for (std::size_t i = 0; i < kFrames * kChannels; ++i) {
        if (kKnown[i] < 0.0f) {
            saw_negative = true;
        }
        if (kKnown[i] > 0.0f) {
            saw_positive = true;
        }
        // One quantisation step is 1/32768; allow a hair over for the round-to-nearest.
        if (std::fabs(back[i] - kKnown[i]) > 1.0f / 32768.0f + 1e-7f) {
            return false;
        }
    }
    return saw_negative && saw_positive; // the buffer must exercise both signs
}

// ---- the checks moved from the audio CLI's tests, over mono signals of a few thousand samples --------

std::vector<float> tone(std::size_t n, double freq, double amplitude) {
    std::vector<float> xs(n);
    for (std::size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<float>(amplitude * std::sin(0.001 * freq * static_cast<double>(i)));
    }
    return xs;
}

// Everything left in `r`, read 1024 frames at a time.
std::vector<float> drain(wav_reader& r) {
    const auto width = static_cast<std::size_t>(std::max(1, r.channels()));
    std::vector<float> out;
    std::vector<float> chunk(1024 * width);
    for (std::size_t got = r.read(chunk.data(), 1024); got != 0; got = r.read(chunk.data(), 1024)) {
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got * width));
    }
    return out;
}

std::vector<float> read_back(const std::string& path, std::int64_t* frames = nullptr) {
    wav_reader r;
    if (!r.open(path.c_str())) {
        return {};
    }
    if (frames != nullptr) {
        *frames = r.frames();
    }
    return drain(r);
}

bool write_by_path(const std::string& path, const std::vector<float>& x, wav_format fmt = wav_format::f32) {
    wav_writer w;
    return w.open(path.c_str(), 48000, 1, fmt) && w.write(x.data(), x.size()) && w.close();
}

std::vector<unsigned char> drain_fd(int fd) {
    std::vector<unsigned char> bytes;
    unsigned char chunk[4096];
    for (ssize_t got = ::read(fd, chunk, sizeof chunk); got > 0; got = ::read(fd, chunk, sizeof chunk)) {
        bytes.insert(bytes.end(), chunk, chunk + got);
    }
    return bytes;
}

// A WAV's RIFF and data sizes, as its header holds them.
using Sizes = std::pair<std::uint32_t, std::uint32_t>;
constexpr Sizes kPlaceholders{0xFFFFFFFFu, 0xFFFFFFFFu};
Sizes header_sizes(const unsigned char* h) {
    const auto u32 = [&](int a) { return std::uint32_t{h[a]} | std::uint32_t{h[a + 1]} << 8 | std::uint32_t{h[a + 2]} << 16 | std::uint32_t{h[a + 3]} << 24; };
    return {u32(4), u32(40)};
}
Sizes header_sizes(const std::string& path) {
    unsigned char h[44]{};
    std::FILE* f = std::fopen(path.c_str(), "rb");
    const bool whole = f != nullptr && std::fread(h, 1, 44, f) == 44;
    if (f != nullptr) {
        std::fclose(f);
    }
    return whole ? header_sizes(h) : Sizes{};
}

// `open(FILE*)` reads a whole file from a stream: a regular file behind a FILE* (stdin), and a pipe.
void read_from_a_stream(const std::vector<float>& a) { // `a` under 4 KiB as a WAV: it fits any pipe
    const std::string path = at("in.wav");
    write_by_path(path, a);
    std::FILE* f = std::fopen(path.c_str(), "rb");
    wav_reader r;
    const bool opened = f != nullptr && r.open(f);
    const std::vector<float> back = opened ? drain(r) : std::vector<float>{};
    r.close();
    if (f != nullptr) {
        std::fclose(f);
    }
    expect(opened && back == a, "open(FILE*) reads a whole file from a stream");

    std::vector<unsigned char> bytes(44 + 4 * a.size());
    std::FILE* in = std::fopen(path.c_str(), "rb");
    const bool read = in != nullptr && std::fread(bytes.data(), 1, bytes.size(), in) == bytes.size();
    if (in != nullptr) {
        std::fclose(in);
    }
    int ends[2]{-1, -1};
    const bool piped = ::pipe(ends) == 0 && ::write(ends[1], bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
    ::close(ends[1]);
    std::FILE* pf = piped ? ::fdopen(ends[0], "rb") : nullptr;
    if (pf == nullptr) {
        ::close(ends[0]);
    }
    wav_reader rp;
    const bool popened = pf != nullptr && rp.open(pf);
    const std::vector<float> pback = popened ? drain(rp) : std::vector<float>{};
    rp.close();
    if (pf != nullptr) {
        std::fclose(pf);
    }
    expect(read && piped && popened && pback == a, "open(FILE*) reads a whole file from a pipe");
}

// Written through `open(FILE*)` onto a seekable file (the CLI's `-o -` onto a redirected stdout): the
// placeholders stay, since where the stream's WAV starts is not the writer's to know (e6cdcbc7); the
// reader reads it to its end and reports no frame count.
void placeholders_of_a_stream(const std::vector<float>& b) {
    const std::string path = at("stdout.wav");
    std::FILE* f = std::fopen(path.c_str(), "wb");
    wav_writer w;
    const bool ok = f != nullptr && w.open(f, 48000, 1) && w.write(b.data(), b.size()) && w.close();
    if (f != nullptr) {
        std::fclose(f);
    }
    std::int64_t frames = 0;
    const std::vector<float> back = read_back(path, &frames);
    expect(ok && header_sizes(path) == kPlaceholders && frames == -1 && back == b,
           "open(FILE*) onto a seekable file keeps the placeholders; read to its end, frames() == -1");
}

// Little-endian writers for a hand-made header.
void put16(std::FILE* f, std::uint32_t v) {
    std::fputc(static_cast<int>(v & 0xFF), f);
    std::fputc(static_cast<int>((v >> 8) & 0xFF), f);
}
void put32(std::FILE* f, std::uint32_t v) {
    put16(f, v & 0xFFFF);
    put16(f, v >> 16);
}

// A header claiming 65535 channels of 16 bits (a frame of 128 KiB, which overflowed the reader's 64 KiB
// buffer), and a chunk declaring 0xFFFFFFFF bytes before the fmt (its size and pad byte wrapped to 0, and
// the reader read the chunk's payload as the header).
void hostile_headers_refused() {
    const std::string huge = at("huge.wav");
    std::FILE* f = std::fopen(huge.c_str(), "wb");
    const std::uint32_t data = 65535u * 2u;
    std::fputs("RIFF", f), put32(f, 36 + data), std::fputs("WAVEfmt ", f), put32(f, 16), put16(f, 1), put16(f, 65535);
    put32(f, 48000), put32(f, 48000 * data), put16(f, 0), put16(f, 16), std::fputs("data", f), put32(f, data);
    for (std::uint32_t i = 0; i < data; ++i) {
        std::fputc(0, f);
    }
    std::fclose(f);
    wav_reader r;
    expect(!r.open(huge.c_str()) && std::string_view(r.error()).find("64 KiB") != std::string_view::npos,
           "a frame over 64 KiB is refused by name, not read past the buffer");

    const std::string wrap = at("wrap.wav");
    f = std::fopen(wrap.c_str(), "wb");
    std::fputs("RIFF", f), put32(f, 0xFFFFFFFFu), std::fputs("WAVEjunk", f), put32(f, 0xFFFFFFFFu);
    std::fputs("fmt ", f), put32(f, 16), put16(f, 3), put16(f, 1), put32(f, 8000), put32(f, 32000), put16(f, 4), put16(f, 32);
    std::fputs("data", f), put32(f, 8), put32(f, 0), put32(f, 0);
    std::fclose(f);
    wav_reader wrapped;
    expect(!wrapped.open(wrap.c_str()), "a chunk of 0xFFFFFFFF bytes is skipped as that, not as none");
}

// A data chunk followed by another chunk (a `LIST` after the samples, as editors write): the reader
// stops at the data's end, both in one call longer than its 64 KiB buffer and in chunks.
void read_past_a_trailing_chunk() {
    const std::vector<float> x = tone(20000, 5.0, 0.5); // 80 000 bytes of samples: more than the buffer holds
    const std::string path = at("trailing.wav");
    const bool written = write_by_path(path, x);
    std::FILE* f = std::fopen(path.c_str(), "ab");
    const bool appended = f != nullptr;
    if (appended) {
        std::fputs("LIST", f), put32(f, 4), std::fputs("junk", f);
        std::fclose(f);
    }
    wav_reader whole;
    std::vector<float> back(x.size() + 8);
    back.resize(whole.open(path.c_str()) ? whole.read(back.data(), back.size()) : 0);
    expect(written && appended && back == x && read_back(path) == x, "a chunk after the data is not read as samples, in one call or in chunks");
}

// How far an s16 WAV of `x` lies from each sample rounded to the nearest step and clamped at full scale,
// in steps (0 when it is exactly that).
double steps_off(const std::vector<float>& x, const std::vector<float>& y) {
    if (x.size() != y.size()) {
        return 1e9;
    }
    double w = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float step = std::clamp(static_cast<float>(std::lrintf(x[i] * 32768.0f)), -32768.0f, 32767.0f);
        w = std::fmax(w, std::fabs(static_cast<double>(y[i]) * 32768.0 - static_cast<double>(step)));
    }
    return w;
}

// s16: each sample the nearest step (not merely within one), and clamped at full scale at both ends.
void s16_steps_and_clamp(const std::vector<float>& a) {
    std::vector<float> x(a.size());
    std::transform(a.begin(), a.end(), x.begin(), [](float v) { return v * 1.5f; }); // peak 0.75
    const std::string nearest = at("y16.wav");
    expect(write_by_path(nearest, x, wav_format::s16) && steps_off(x, read_back(nearest)) == 0.0, "s16: each sample the nearest step");
    std::transform(a.begin(), a.end(), x.begin(), [](float v) { return v * 3.0f; }); // peak 1.5, both signs
    const std::string clipped = at("y16c.wav");
    const bool written = write_by_path(clipped, x, wav_format::s16);
    const std::vector<float> y = read_back(clipped);
    const bool top = std::find(y.begin(), y.end(), 32767.0f / 32768.0f) != y.end();
    const bool bottom = std::find(y.begin(), y.end(), -1.0f) != y.end();
    expect(written && steps_off(x, y) == 0.0 && top && bottom, "s16: clamped at full scale");
}

// A frame's bytes are a 16-bit field of the header and a second's a 32-bit one: each too wide is refused
// by its own text before the file is opened, so the file keeps what it held. 16384 f32 channels at 48 kHz
// overflow the frame and not the second (70000 channels, the CLI test's case, overflow both).
void too_wide_refused_untouched() {
    const std::string keep = at("keep.wav");
    std::FILE* held = std::fopen(keep.c_str(), "wb");
    std::fputs("KEEP-ME", held);
    std::fclose(held);
    wav_writer frame;
    const bool no_frame = !frame.open(keep.c_str(), 48000, 16384, wav_format::f32) &&
                          std::string_view(frame.error()) == "wav: too many channels for a WAV frame";
    wav_writer second;
    const bool no_second = !second.open(keep.c_str(), 1 << 30, 1) && std::string_view(second.error()) == "wav: too many bytes a second for a WAV header";
    expect(no_frame && no_second && std::filesystem::file_size(keep) == 7, "a frame, or a second, too wide for a WAV header: refused, the file as it was");
}

// `open(path)` on a regular file (the CLI opened stdout's regular file by its name): the RIFF and data
// sizes patched at `close()`.
void sizes_patched_by_path(const std::vector<float>& b) {
    const std::string path = at("named.wav");
    const bool ok = write_by_path(path, b);
    const Sizes patched{static_cast<std::uint32_t>(36 + 4 * b.size()), static_cast<std::uint32_t>(4 * b.size())};
    expect(ok && header_sizes(path) == patched && std::filesystem::file_size(path) == 44 + 4 * b.size() && read_back(path) == b,
           "open(path) on a regular file: its sizes patched at close()");
}

// `open(path)` on a FIFO: the seek fails, the placeholders stay, and that is no failure (045f4829). The
// read end is open (not blocking) first, so the writer's open does not wait; the WAV fits the pipe.
void into_a_fifo(const std::vector<float>& a) {
    const std::string fifo = at("fifo");
    const bool made = ::mkfifo(fifo.c_str(), 0600) == 0;
    const int reader = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);
    wav_writer w;
    const bool wrote = reader >= 0 && w.open(fifo.c_str(), 48000, 1) && w.write(a.data(), a.size());
    const bool closed = w.close();
    const std::vector<unsigned char> bytes = reader >= 0 ? drain_fd(reader) : std::vector<unsigned char>{};
    ::close(reader);
    expect(made && wrote && closed && bytes.size() == 44 + 4 * a.size() && header_sizes(bytes.data()) == kPlaceholders,
           "open(path) on a FIFO: written whole, the failed seek no failure");
}

// A write that fails is reported (e6cdcbc7): a large one by `write()` itself, a small one, still in the
// stream's buffer, only by `close()` — which also pins 045f4829's flush before the seek: without it the
// flush inside `fseek` fails unseen and `fclose` succeeds.
void failed_writes_reported(const std::vector<float>& a) {
    if (::access("/dev/full", W_OK) != 0) {
        return; // no such device here: nothing to write into that always fails
    }
    wav_writer large;
    const bool large_open = large.open("/dev/full", 48000, 1);
    const bool large_written = large.write(a.data(), a.size());
    const bool large_closed = large.close();
    expect(large_open && !large_written && !large_closed && std::string_view(large.error()) == "wav: write failed",
           "onto /dev/full: a large write refused by write()");
    const std::vector<float> s(10, 0.25f);
    wav_writer small;
    const bool small_open = small.open("/dev/full", 48000, 1);
    const bool small_written = small.write(s.data(), s.size());
    const bool small_closed = small.close();
    expect(small_open && small_written && !small_closed && std::string_view(small.error()) == "wav: write failed",
           "onto /dev/full: a small write refused by close()");
    // the same through a stream the writer was handed (`-o -` onto a full disk): its flush is checked too
    std::FILE* full = std::fopen("/dev/full", "wb");
    wav_writer handed;
    const bool handed_written = full != nullptr && handed.open(full, 48000, 1) && handed.write(s.data(), s.size());
    const bool handed_closed = handed.close();
    if (full != nullptr) {
        std::fclose(full);
    }
    expect(handed_written && !handed_closed && std::string_view(handed.error()) == "wav: write failed",
           "onto /dev/full through open(FILE*): a small write refused by close()");
}

// `open(FILE*)` onto a socket (stdout as a Node child's or the journal's): no seek, `close()` true, and the
// whole WAV on the other end.
void onto_a_socket(const std::vector<float>& b) {
    int ends[2]{-1, -1};
    const bool pair = ::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0;
    std::FILE* f = pair ? ::fdopen(ends[0], "wb") : nullptr;
    wav_writer w;
    const bool wrote = f != nullptr && w.open(f, 48000, 1) && w.write(b.data(), b.size());
    const bool closed = w.close();
    if (f != nullptr) {
        std::fclose(f);
    }
    if (f == nullptr) {
        ::close(ends[0]); // or the drain below waits for an end nobody closes
    }
    const std::vector<unsigned char> bytes = pair ? drain_fd(ends[1]) : std::vector<unsigned char>{};
    ::close(ends[1]);
    expect(wrote && closed && bytes.size() == 44 + 4 * b.size(), "open(FILE*) onto a socket: written whole, close() true");
}

} // namespace

int main() {
    std::filesystem::create_directories(kDir);
    expect(float32_is_bit_exact(), "float32 read back bit for bit, the format intact");
    expect(s16_round_trips_within_a_step(), "s16 within one step, across both signs");
    const std::vector<float> a = tone(3000, 7.0, 0.5);
    const std::vector<float> b = tone(1234, 13.0, 0.5);
    const std::vector<float> small = tone(900, 11.0, 0.5); // 3644 bytes as a WAV: under 4 KiB
    read_from_a_stream(small);
    read_past_a_trailing_chunk();
    placeholders_of_a_stream(b);
    hostile_headers_refused();
    s16_steps_and_clamp(a);
    too_wide_refused_untouched();
    sizes_patched_by_path(b);
    into_a_fifo(small);
    failed_writes_reported(a);
    onto_a_socket(small);
    std::filesystem::remove_all(kDir);
    if (g_wrong != 0) {
        std::printf("wav_roundtrip_test: %d wrong\n", g_wrong);
        return 1;
    }
    std::printf("wav_roundtrip_test: the WAV reader and writer hold\n");
    return 0;
}
