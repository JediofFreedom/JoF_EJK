#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include "nav_file_reader.h"

#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #test); std::exit(1); } } while (0)
#define STL_INSERT(container, value) (container).push_back(value)
#define STL_ITERATE(it, container) for ((it) = (container).begin(); (it) != (container).end(); ++(it))
#define VectorCopy(from, to) std::memcpy((to), (from), sizeof(vec3_t))
typedef unsigned char byte;
typedef float vec3_t[3];
typedef int qboolean;
typedef int fileHandle_t;
enum { NAV_HEADER_ID = 0x4a4e5635, NODE_HEADER_ID = 0x4e4f4445, MAX_FAILED_EDGES = 32,
       WAYPOINT_NONE = -1, ENTITYNUM_NONE = 1023, MAX_GENTITIES = 1024, EFLAG_NONE = 0, FS_READ, FS_WRITE };
struct failedEdge_t { int startID, endID, checkTime, entID; };
typedef std::multimap<int, int> EdgeMultimap;
static int liveNodes;
#include "node.h"
class CNavigator {
    typedef std::vector<CNode *> node_v;
public:
    failedEdge_t failedEdges[MAX_FAILED_EDGES];
    node_v m_nodes;
    EdgeMultimap m_edgeLookupMap;
    void Free();
    bool Load(const char *, int);
    bool Save(const char *, int);
    int GetInt(fileHandle_t);
    long GetLong(fileHandle_t);
};
static std::vector<byte> fileData;
static size_t readOffset;
static int openFiles, readCalls;
static int advertisedLength = -1;
static int FS_FOpenFileByMode(const char *, fileHandle_t *file, int mode) {
    CHECK(!openFiles);
    openFiles = 1; *file = 1; readOffset = 0;
    if (mode == FS_WRITE) { fileData.clear(); return 0; }
    return advertisedLength >= 0 ? advertisedLength : int(fileData.size());
}
static int FS_Read(void *data, int length, fileHandle_t file) {
    CHECK(file == 1 && openFiles && length >= 0); ++readCalls;
    const size_t bytes = std::min(size_t(length), fileData.size() - readOffset);
    if (bytes) std::memcpy(data, fileData.data() + readOffset, bytes);
    readOffset += bytes; return int(bytes);
}
static int FS_Write(const void *data, int length, fileHandle_t file) {
    CHECK(file == 1 && openFiles && length >= 0);
    const byte *bytes = static_cast<const byte *>(data);
    fileData.insert(fileData.end(), bytes, bytes + length); return length;
}
static void FS_FCloseFile(fileHandle_t file) { CHECK(file == 1 && openFiles); openFiles = 0; }
static const char *va(const char *format, ...) {
    static char path[128]; va_list args; va_start(args, format);
    std::vsnprintf(path, sizeof(path), format, args); va_end(args); return path;
}
#include "navigation.h"

static const int checksum = 42;
static std::vector<byte> fixture(int count) {
    CNavigator nav; nav.Free();
    for (int id = 0; id < count; ++id) {
        CNode *node = CNode::Create();
        for (int axis = 0; axis < 3; ++axis) node->m_position[axis] = float(id * 3 + axis);
        node->m_flags = id; node->m_ID = id; node->m_radius = 16;
        node->m_numEdges = 1; node->m_edges.resize(1);
        node->m_edges[0].ID = (id + 1) % count;
        node->m_edges[0].cost = id + 100;
        node->m_edges[0].flags = byte(id & 1);
        node->InitRanks(count);
        for (int rank = 0; rank < count; ++rank) node->m_ranks[rank] = rank;
        nav.m_nodes.push_back(node);
    }
    if (count > 1) nav.failedEdges[0] = {0, 1, 1000, 3};
    CHECK(nav.Save("fixture", checksum));
    const auto data = fileData;
    nav.Free(); CHECK(!liveNodes && !openFiles);
    return data;
}
static size_t nodeOffset(int index, int count) { return 12 + size_t(index) * (36 + 12 + size_t(count) * 4); }
static void putInt(std::vector<byte> &data, size_t offset, int value) {
    CHECK(offset + sizeof(value) <= data.size()); std::memcpy(data.data() + offset, &value, sizeof(value));
}
static void rejected(std::vector<byte> data) {
    fileData = std::move(data);
    CNavigator nav; nav.Free();
    CHECK(!nav.Load("fixture", checksum));
    CHECK(nav.m_nodes.empty() && nav.m_edgeLookupMap.empty() && !liveNodes && !openFiles);
    for (const auto &edge : nav.failedEdges) CHECK(edge.startID == WAYPOINT_NONE && edge.endID == WAYPOINT_NONE);
}
static void valid() {
    for (int count : {0, 1, 3, 128}) {
        const auto original = fixture(count);
        CNavigator nav; nav.Free(); readCalls = 0;
        fileData = original;
        CHECK(nav.Load("fixture", checksum));
        CHECK(int(nav.m_nodes.size()) == count && liveNodes == count && !openFiles);
        CHECK(readCalls <= count * 10 + 4); // Rank-table reads grow per node, not per rank.
        CHECK(nav.Save("roundtrip", checksum)); CHECK(fileData == original);
        nav.Free(); CHECK(!liveNodes);
    }
}
static void ownership() {
    auto bytes = fixture(3);
    putInt(bytes, nodeOffset(1, 3), 0); // Fail after the first node was already inserted.
    rejected(bytes);
}
static void truncations() {
    const auto original = fixture(3);
    for (size_t length = 0; length < original.size(); ++length)
        rejected(std::vector<byte>(original.begin(), original.begin() + length));
    // The file length can become stale after opening; a short read must also fail.
    advertisedLength = int(original.size());
    for (size_t length : {size_t(8), nodeOffset(1, 3) + 35, original.size() - 1})
        rejected(std::vector<byte>(original.begin(), original.begin() + length));
    advertisedLength = -1;
}
static void invalid() {
    const auto original = fixture(3);
    for (const auto &field : std::vector<std::pair<size_t, int>>{
            {0, 0}, {4, checksum + 1}, {8, -1}, {8, INT_MAX},
            {nodeOffset(0, 3) + 20, -1}, {nodeOffset(0, 3) + 20, 3},
            {nodeOffset(0, 3) + 28, -1}, {nodeOffset(0, 3) + 28, INT_MAX},
            {nodeOffset(0, 3) + 32, -1}, {nodeOffset(0, 3) + 32, 3},
            {nodeOffset(0, 3) + 44, -1}, {nodeOffset(0, 3) + 44, 2},
            {nodeOffset(3, 3), 3}, {nodeOffset(3, 3) + 4, 3},
            {nodeOffset(3, 3) + 12, MAX_GENTITIES}}) {
        auto bytes = original; putInt(bytes, field.first, field.second); rejected(bytes);
    }
    // A later failed load also clears a previously loaded graph.
    CNavigator nav; nav.Free(); fileData = original; CHECK(nav.Load("fixture", checksum));
    fileData.clear(); CHECK(!nav.Load("empty", checksum)); CHECK(!liveNodes && nav.m_nodes.empty() && !openFiles);
}
static void metrics() {
    fileData = fixture(128); readCalls = 0;
    CNavigator nav; nav.Free(); CHECK(nav.Load("fixture", checksum));
    std::printf("128-node navigation load: %d filesystem reads\n", readCalls);
    nav.Free();
}
static void legacyFile(const char *filename) {
    std::ifstream stream(filename, std::ios::binary); CHECK(stream.good());
    fileData.assign(std::istreambuf_iterator<char>(stream), {});
    CHECK(fileData.size() >= 12);
    int mapChecksum; std::memcpy(&mapChecksum, fileData.data() + 4, sizeof(mapChecksum));
    const auto original = fileData;
    CNavigator nav; nav.Free(); CHECK(nav.Load("fixture", mapChecksum));
    std::printf("%s: %zu nodes, %d filesystem reads\n", filename, nav.m_nodes.size(), readCalls);
    CHECK(nav.Save("roundtrip", mapChecksum)); CHECK(fileData == original);
    nav.Free();
}
int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 3);
    if (!std::strcmp(argv[1], "valid")) valid();
    else if (!std::strcmp(argv[1], "ownership")) ownership();
    else if (!std::strcmp(argv[1], "truncations")) truncations();
    else if (!std::strcmp(argv[1], "invalid")) invalid();
    else if (!std::strcmp(argv[1], "metrics")) metrics();
    else if (argc == 3 && !std::strcmp(argv[1], "file")) legacyFile(argv[2]);
    else return 1;
    CHECK(!liveNodes && !openFiles); return 0;
}
