#include "vk_prepared_cache.h"
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "../../lib/xxhash/xxhash.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <ctime>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace PreparedTextures {
static uint32_t read32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static uint64_t read64(const unsigned char *p) { return read32(p) | uint64_t(read32(p + 4)) << 32; }
static void write32(unsigned char *p, uint32_t value) { for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i)); }
static void write64(unsigned char *p, uint64_t value) { write32(p, uint32_t(value)); write32(p + 4, uint32_t(value >> 32)); }
static Key hash(const void *data, size_t size) { const auto v = XXH3_128bits(data, size); return {v.low64, v.high64}; }
static void writeKey(unsigned char *p, Key key) { write64(p, key.low); write64(p + 8, key.high); }
static Key readKey(const unsigned char *p) { return {read64(p), read64(p + 8)}; }

Key ContentKey(const void *source, size_t length, const void *settings, size_t settingsLength) {
    // Domain/version and length delimit the key inputs. Bump this version when
    // mip generation, greyscale, compression or color processing changes.
    // v2 also invalidates results produced by the old 32-bit resample fractions.
    const unsigned char domain[] = {'E','J','K','-','T','X','C',2};
    unsigned char lengths[16]; write64(lengths, length); write64(lengths + 8, settingsLength);
    XXH3_state_t state;
    XXH3_128bits_reset(&state);
    XXH3_128bits_update(&state, domain, sizeof(domain));
    XXH3_128bits_update(&state, lengths, sizeof(lengths));
    XXH3_128bits_update(&state, source, length);
    XXH3_128bits_update(&state, settings, settingsLength);
    const auto key = XXH3_128bits_digest(&state);
    return {key.low64, key.high64};
}
std::string FileName(Key key) {
    char text[37];
    std::snprintf(text, sizeof(text), "%016llx%016llx.dat", static_cast<unsigned long long>(key.high), static_cast<unsigned long long>(key.low));
    return text;
}
static bool powerOfTwo(uint32_t v) { return v && v <= 32768 && !(v & (v - 1)); }
size_t PayloadSize(const Description &d) {
    if (!powerOfTwo(d.width) || !powerOfTwo(d.height) || !powerOfTwo(d.uploadWidth) || !powerOfTwo(d.uploadHeight) ||
        d.uploadWidth > d.width || d.uploadHeight > d.height || d.levels == 0 || d.levels > 16 || uint32_t(d.format) > uint32_t(Format::BC3)) return 0;
    uint32_t width = d.uploadWidth, height = d.uploadHeight;
    size_t size = 0;
    for (uint32_t level = 0; level < d.levels; ++level) {
        const size_t bytes = d.format == Format::BC3 ? size_t((width + 3) / 4) * ((height + 3) / 4) * 16 : size_t(width) * height * 4;
        if (bytes > MaxPayload - size) return 0;
        size += bytes;
        if (width == 1 && height == 1 && level + 1 < d.levels) return 0;
        if (level + 1 == d.levels && d.levels != 1 && (width != 1 || height != 1)) return 0;
        width = std::max(1u, width / 2); height = std::max(1u, height / 2);
    }
    return size;
}
bool WriteHeader(unsigned char *header, const Description &d, Key key, const unsigned char *data, size_t size) {
    if (!header || !data || !size || PayloadSize(d) != size) return false;
    std::memset(header, 0, HeaderSize);
    std::memcpy(header, "EJKTXC01", 8); write32(header + 8, 1); write32(header + 12, HeaderSize);
    write32(header + 16, d.width); write32(header + 20, d.height);
    write32(header + 24, d.uploadWidth); write32(header + 28, d.uploadHeight);
    write32(header + 32, d.levels); write32(header + 36, uint32_t(d.format)); write32(header + 40, uint32_t(size));
    writeKey(header + 48, key); writeKey(header + 64, hash(data, size)); writeKey(header + 80, hash(header, 80));
    return true;
}
bool ReadHeader(const unsigned char *header, size_t fileSize, Key expected, uint32_t width, uint32_t height,
                uint32_t allowedFormats, Description &d) {
    if (!header || fileSize < HeaderSize || fileSize > HeaderSize + MaxPayload || std::memcmp(header, "EJKTXC01", 8) ||
        read32(header + 8) != 1 || read32(header + 12) != HeaderSize || read32(header + 44) ||
        !(readKey(header + 48) == expected) || !(readKey(header + 80) == hash(header, 80))) return false;
    for (size_t i = 96; i < HeaderSize; ++i) if (header[i]) return false;
    Description decoded = {read32(header + 16), read32(header + 20), read32(header + 24), read32(header + 28),
                           read32(header + 32), static_cast<Format>(read32(header + 36))};
    if (decoded.width != width || decoded.height != height || uint32_t(decoded.format) > uint32_t(Format::BC3) ||
        !(allowedFormats & (1u << uint32_t(decoded.format)))) return false;
    const size_t size = PayloadSize(decoded);
    if (!size || size != fileSize - HeaderSize || size != read32(header + 40)) return false;
    d = decoded;
    return true;
}
bool VerifyPayload(const unsigned char *header, const unsigned char *data, size_t size) {
    return header && data && size && size <= MaxPayload && read32(header + 40) == size && readKey(header + 64) == hash(data, size);
}

struct File { std::string name; uint64_t size; time_t modified; };
static bool entryName(const std::string &name, bool temporary = false) {
    if (name.size() < 36) return false;
    for (size_t i = 0; i < 32; ++i) if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return false;
    if (!temporary) return name.size() == 36 && name.substr(32) == ".dat";
    if (name.substr(32, 5) != ".tmp-") return false;
    for (size_t i = 37; i < name.size(); ++i) if (name[i] != '-' && (name[i] < '0' || name[i] > '9')) return false;
    return name.size() > 37;
}
static std::vector<File> scan(const std::string &directory) {
    std::vector<File> files;
    const auto add = [&](const char *name) {
        const std::string path = directory + "/" + name;
        // Only a process holding the writer lock removes stale temp entries.
        if (entryName(name, true)) { std::remove(path.c_str()); return; }
        if (!entryName(name)) return;
        struct stat info;
        if (stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFMT) == S_IFREG && info.st_size >= 0)
            files.push_back({name, static_cast<uint64_t>(info.st_size), info.st_mtime});
    };
#ifdef _WIN32
    WIN32_FIND_DATAA info;
    HANDLE find = FindFirstFileA((directory + "/*").c_str(), &info);
    if (find != INVALID_HANDLE_VALUE) { do { if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) add(info.cFileName); } while (FindNextFileA(find, &info)); FindClose(find); }
#else
    DIR *dir = opendir(directory.c_str());
    if (dir) { while (dirent *info = readdir(dir)) add(info->d_name); closedir(dir); }
#endif
    std::sort(files.begin(), files.end(), [](const File &a, const File &b) { return a.modified < b.modified; });
    return files;
}

struct Cache::State {
    std::string directory;
    size_t diskBudget = 0, queueBudget = 0, queuedBytes = 0;
    std::atomic<uint64_t> diskBytes{0};
    std::atomic<uint64_t> hits{0}, misses{0}, writes{0}, dropped{0}, invalid{0};
    std::atomic<bool> failed{false};
    struct Pending { std::string name; std::vector<unsigned char> file; };
    std::deque<Pending> pending;
    mutable std::mutex mutex;
    std::condition_variable ready;
    bool stopping = false;
    std::thread writer;
    std::vector<File> files;
#ifdef _WIN32
    HANDLE writerLock = INVALID_HANDLE_VALUE;
    bool lock() { writerLock = CreateFileA((directory + "/writer.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); return writerLock != INVALID_HANDLE_VALUE; }
    void unlock() { CloseHandle(writerLock); writerLock = INVALID_HANDLE_VALUE; }
#else
    int writerLock = -1;
    bool lock() { writerLock = open((directory + "/writer.lock").c_str(), O_CREAT | O_RDWR, 0600); if (writerLock >= 0 && flock(writerLock, LOCK_EX | LOCK_NB) == 0) return true; if (writerLock >= 0) close(writerLock); writerLock = -1; return false; }
    void unlock() { close(writerLock); writerLock = -1; }
#endif
    bool trim(size_t incoming, const std::string &protectedName = std::string()) {
        for (auto it = files.begin(); diskBytes.load() > diskBudget - incoming && it != files.end();) {
            if (it->name == protectedName) { ++it; continue; }
            if (std::remove((directory + "/" + it->name).c_str()) == 0) {
                diskBytes.fetch_sub(it->size); it = files.erase(it);
            } else ++it;
        }
        return diskBytes.load() <= diskBudget - incoming;
    }
    void run() {
        files = scan(directory);
        for (const auto &file : files) diskBytes.fetch_add(file.size);
        trim(0);
        uint64_t serial = 0;
        for (;;) {
            Pending job;
            {
                std::unique_lock<std::mutex> guard(mutex);
                ready.wait(guard, [&]() { return stopping || !pending.empty(); });
                if (pending.empty()) break;
                job = std::move(pending.front()); pending.pop_front();
            }
            const std::string target = directory + "/" + job.name;
            // The exclusive process lock prevents another writer changing this
            // index or consuming the bounded disk budget behind our back.
            bool success = false;
            if (job.file.size() <= diskBudget && trim(job.file.size(), job.name)) {
                const std::string temporary = directory + "/" + job.name.substr(0, 32) + ".tmp-" + std::to_string(++serial);
                FILE *file = std::fopen(temporary.c_str(), "wb");
                if (file) {
                    const bool written = std::fwrite(job.file.data(), 1, job.file.size(), file) == job.file.size();
                    const bool closed = std::fclose(file) == 0;
                    if (written && closed) {
#ifdef _WIN32
                        success = MoveFileExA(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
                        success = std::rename(temporary.c_str(), target.c_str()) == 0;
#endif
                    }
                    if (!success) std::remove(temporary.c_str());
                }
            }
            if (success) {
                for (auto it = files.begin(); it != files.end(); ++it) if (it->name == job.name) {
                    diskBytes.fetch_sub(it->size); files.erase(it); break;
                }
                diskBytes.fetch_add(job.file.size()); files.push_back({job.name, job.file.size(), std::time(NULL)}); ++writes;
            }
            else ++dropped;
            { std::lock_guard<std::mutex> guard(mutex); queuedBytes -= job.file.size(); }
        }
        unlock();
    }
};
Cache::Cache() : state(new State) {}
Cache::~Cache() { shutdown(); }
void Cache::shutdown() {
    { std::lock_guard<std::mutex> guard(state->mutex); state->stopping = true; }
    state->ready.notify_all();
    if (state->writer.joinable()) state->writer.join();
}
bool Cache::configure(const std::string &directory, size_t diskBudget, size_t queueBudget) {
    if (state->directory == directory && state->diskBudget == diskBudget && state->queueBudget == queueBudget && !state->stopping) return true;
    shutdown();
    state.reset(new State);
    state->directory = directory; state->diskBudget = diskBudget; state->queueBudget = queueBudget;
    if (!diskBudget || directory.empty()) return false;
    if (!state->lock()) return true; // Read-only when another client owns the writer.
    try { state->writer = std::thread([this]() {
        try { state->run(); }
        catch (...) {
            // A best-effort cache must not terminate the game on allocation or
            // directory enumeration failure in its background writer.
            state->failed.store(true);
            { std::lock_guard<std::mutex> guard(state->mutex); state->pending.clear(); state->queuedBytes = 0; }
            state->unlock();
        }
    }); }
    catch (...) { state->unlock(); return false; }
    return true;
}
bool Cache::read(Key key, uint32_t width, uint32_t height, uint32_t allowedFormats, Texture &texture) {
    texture = {};
    if (!state->diskBudget || state->directory.empty()) return false;
    FILE *file = std::fopen((state->directory + "/" + FileName(key)).c_str(), "rb");
    if (!file) { ++state->misses; return false; }
    unsigned char header[HeaderSize];
    Description description;
    bool valid = std::fseek(file, 0, SEEK_END) == 0;
    const long length = valid ? std::ftell(file) : -1;
    valid = valid && length >= long(HeaderSize) && size_t(length) <= HeaderSize + MaxPayload &&
        std::fseek(file, 0, SEEK_SET) == 0 && std::fread(header, 1, sizeof(header), file) == sizeof(header) &&
        ReadHeader(header, size_t(length), key, width, height, allowedFormats, description);
    if (valid) {
        try { texture.bytes.resize(size_t(length) - HeaderSize); } catch (...) { valid = false; }
        if (valid) valid = std::fread(texture.bytes.data(), 1, texture.bytes.size(), file) == texture.bytes.size() &&
            VerifyPayload(header, texture.bytes.data(), texture.bytes.size());
    }
    std::fclose(file);
    if (!valid) { texture = {}; ++state->invalid; ++state->misses; return false; }
    texture.description = description; ++state->hits;
    return true;
}
bool Cache::store(Key key, const Description &description, const void *data, size_t size) {
    if (!state->writer.joinable() || state->failed.load() || !data || !size || size > MaxPayload || PayloadSize(description) != size) return false;
    const size_t bytes = HeaderSize + size;
    std::lock_guard<std::mutex> guard(state->mutex);
    if (state->stopping || state->failed.load() || bytes > state->diskBudget || bytes > state->queueBudget || state->queuedBytes > state->queueBudget - bytes) { ++state->dropped; return false; }
    try {
        State::Pending pending; pending.name = FileName(key); pending.file.resize(bytes);
        if (!WriteHeader(pending.file.data(), description, key, static_cast<const unsigned char *>(data), size)) return false;
        std::memcpy(pending.file.data() + HeaderSize, data, size);
        state->pending.push_back(std::move(pending)); state->queuedBytes += bytes;
    } catch (...) { ++state->dropped; return false; }
    state->ready.notify_one();
    return true;
}
Statistics Cache::statistics() const {
    std::lock_guard<std::mutex> guard(state->mutex);
    return {state->hits.load(), state->misses.load(), state->writes.load(), state->dropped.load(), state->invalid.load(), state->queuedBytes, state->diskBytes.load()};
}
}
