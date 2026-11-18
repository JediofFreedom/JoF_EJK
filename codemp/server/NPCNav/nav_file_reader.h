#ifndef NAV_FILE_READER_H
#define NAV_FILE_READER_H
#include <cstddef>
#include <climits>

// A navigation file keeps its existing native serialization. Count checks use
// the remaining file size before allocating or reading any variable-size table.
class NavFileReader {
    int file;
    size_t left;
    int (*readFile)(void *, int, int);
public:
    NavFileReader(int handle, size_t length, int (*read)(void *, int, int))
        : file(handle), left(length), readFile(read) {}
    size_t remaining() const { return left; }
    bool readBytes(void *data, size_t bytes) {
        if (bytes > left || bytes > INT_MAX) return false;
        if (bytes && readFile(data, static_cast<int>(bytes), file) != static_cast<int>(bytes)) return false;
        left -= bytes;
        return true;
    }
    template<class T> bool read(T &value) { return readBytes(&value, sizeof(value)); }
};
#endif
