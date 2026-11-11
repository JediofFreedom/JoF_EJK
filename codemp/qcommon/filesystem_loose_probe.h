// Live missing-file probe for metadata lookups. Never cache loose directory
// contents: additions, edits and deletions must affect the next lookup.
#ifndef EJK_FILESYSTEM_LOOSE_PROBE_H
#define EJK_FILESYSTEM_LOOSE_PROBE_H
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static inline bool FS_LooseFileDefinitelyMissing(const char *path) {
#ifdef _WIN32
    // Keep CRT path handling for non-ASCII paths and DOS device aliases. Their
    // fopen interpretation need not match an ANSI file-attribute lookup.
    const char *leaf = path;
    for (const unsigned char *ch = reinterpret_cast<const unsigned char *>(path); *ch; ++ch) {
        if (*ch >= 128) return false;
        if (*ch == '/' || *ch == '\\') leaf = reinterpret_cast<const char *>(ch + 1);
    }
    char stem[9] = {};
    size_t length = 0;
    while (length < sizeof(stem) - 1 && leaf[length] && leaf[length] != '.' &&
           leaf[length] != ':' && leaf[length] != ' ') {
        const char ch = leaf[length];
        stem[length++] = ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
    }
    if (!std::strcmp(stem, "CON") || !std::strcmp(stem, "PRN") || !std::strcmp(stem, "AUX") ||
        !std::strcmp(stem, "NUL") || !std::strcmp(stem, "CLOCK$") ||
        !std::strcmp(stem, "CONIN$") || !std::strcmp(stem, "CONOUT$") ||
        (length == 4 && stem[3] >= '0' && stem[3] <= '9' &&
         ((!std::memcmp(stem, "COM", 3)) || (!std::memcmp(stem, "LPT", 3))))) return false;
    // Most texture-source candidates are absent loose overrides of PK3 assets.
    // The native attribute query avoids CRT stream/path setup for those misses.
    // Existing files and uncertain errors still use fopen, preserving readable
    // file and length semantics, including sharing violations and reparse points.
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return false;
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
#else
    (void)path;
    return false;
#endif
}
#endif
