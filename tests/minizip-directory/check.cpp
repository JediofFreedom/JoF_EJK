#include <minizip/unzip.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" void* openjk_minizip_malloc(int size) { return std::malloc(size); }
extern "C" int openjk_minizip_free(void* pointer) { std::free(pointer); return 0; }
extern "C" unsigned long long scalar_fingerprint(void);

static unsigned long long hash_value = 1469598103934665603ULL;
static void hash_bytes(const void* pointer, size_t size) {
    const unsigned char* p = static_cast<const unsigned char*>(pointer);
    while (size--) hash_value = (hash_value ^ *p++) * 1099511628211ULL;
}
static void hash_number(unsigned long long value) {
    for (int i=0; i<8; ++i) {
        unsigned char byte = static_cast<unsigned char>(value);
        hash_bytes(&byte, 1); value >>= 8;
    }
}
struct Callbacks { zlib_filefunc64_def base; unsigned long long reads=0, bytes=0; };
static uLong ZCALLBACK counted_read(voidpf opaque, voidpf stream, void* buffer, uLong size) {
    Callbacks* c = static_cast<Callbacks*>(opaque);
    ++c->reads;
    uLong read = c->base.zread_file(c->base.opaque, stream, buffer, size);
    c->bytes += read;
    return read;
}

int main(int argc, char** argv) {
    bool extract=false, check=false;
    int first=1;
    if (argc>1 && std::strcmp(argv[1], "--extract")==0) { extract=true; ++first; }
    if (argc>first && std::strcmp(argv[first], "--check")==0) { check=true; ++first; }
    unsigned long long entries=0, reads=0, bytes=0, extracted=0;
    unsigned int failures=0, archives=0;
    const auto start = std::chrono::steady_clock::now();
    for (int arg=first; arg<argc; ++arg) {
        Callbacks c;
        fill_fopen64_filefunc(&c.base);
        zlib_filefunc64_def functions=c.base;
        functions.opaque=&c; functions.zread_file=counted_read;
        unzFile file=unzOpen2_64(argv[arg], &functions);
        ++archives;
        hash_number(file ? 1 : 0);
        if (!file) { ++failures; reads+=c.reads; bytes+=c.bytes; continue; }
        unz_global_info64 global;
        int result=unzGetGlobalInfo64(file, &global);
        hash_number(result);
        if (result!=UNZ_OK) { ++failures; unzClose(file); continue; }
        hash_number(global.number_entry); hash_number(global.size_comment);
        std::vector<char> global_comment(global.size_comment+1, '\x7f');
        hash_number(unzGetGlobalComment(file, global_comment.data(), static_cast<uLong>(global_comment.size())));
        hash_bytes(global_comment.data(), global_comment.size());
        std::vector<char> name(65536, '\x7f'), comment(65536, '\x7f');
        std::vector<unsigned char> extra(65536);
        result=unzGoToFirstFile(file);
        while (result==UNZ_OK) {
            unz_file_info64 info;
            std::memset(&info, 0, sizeof(info));
            result=unzGetCurrentFileInfo64(file, &info, name.data(), static_cast<uLong>(name.size()),
                extra.data(), static_cast<uLong>(extra.size()), comment.data(), static_cast<uLong>(comment.size()));
            hash_number(result);
            if (result!=UNZ_OK) { ++failures; break; }
            hash_number(info.version); hash_number(info.version_needed); hash_number(info.flag);
            hash_number(info.compression_method); hash_number(info.dosDate); hash_number(info.crc);
            hash_number(info.compressed_size); hash_number(info.uncompressed_size);
            hash_number(info.size_filename); hash_number(info.size_file_extra); hash_number(info.size_file_comment);
            hash_number(info.disk_num_start); hash_number(info.internal_fa); hash_number(info.external_fa);
            hash_bytes(name.data(), info.size_filename+1); hash_bytes(comment.data(), info.size_file_comment+1);
            hash_bytes(extra.data(), info.size_file_extra);
            unz64_file_pos position;
            hash_number(unzGetFilePos64(file, &position));
            hash_number(position.pos_in_zip_directory); hash_number(position.num_of_file);
            hash_number(unzGetOffset64(file));
            if (check) {
                // Save/restore and locate must agree with sequential navigation.
                hash_number(unzGoToFilePos64(file, &position));
                hash_number(unzGetOffset64(file));
                std::vector<char> short_name(std::min<size_t>(4, info.size_filename+1), '\x7f');
                hash_number(unzGetCurrentFileInfo64(file, NULL, short_name.data(), static_cast<uLong>(short_name.size()), NULL, 0, NULL, 0));
                hash_bytes(short_name.data(), short_name.size());
            }
            if (extract) {
                int open=unzOpenCurrentFile(file); hash_number(open);
                if (open!=UNZ_OK) ++failures;
                else {
                    char buffer[65536]; int read;
                    unsigned long long size=0; uLong crc=crc32(0, Z_NULL, 0);
                    while ((read=unzReadCurrentFile(file, buffer, sizeof(buffer)))>0) {
                        crc=crc32(crc, reinterpret_cast<Bytef*>(buffer), read); size+=read;
                    }
                    int close=unzCloseCurrentFile(file);
                    hash_number(read); hash_number(close); hash_number(size); hash_number(crc);
                    if (read<0 || close!=UNZ_OK || size!=info.uncompressed_size || crc!=info.crc) ++failures;
                    extracted+=size;
                }
            }
            ++entries;
            result=unzGoToNextFile(file);
        }
        hash_number(result);
        if (global.number_entry && result!=UNZ_END_OF_LIST_OF_FILE) ++failures;
        hash_number(unzClose(file)); reads+=c.reads; bytes+=c.bytes;
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::printf("{\"archives\":%u,\"entries\":%llu,\"fingerprint\":\"%016llx\",\"scalar\":\"%016llx\",\"failures\":%u,\"reads\":%llu,\"bytes\":%llu,\"extracted\":%llu,\"ms\":%.6f}\n",
        archives, entries, hash_value, scalar_fingerprint(), failures, reads, bytes, extracted, ms);
    return 0;
}
