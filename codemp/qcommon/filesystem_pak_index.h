// Immutable archive-member candidates plus live directory providers. The caller
// retains the original package hash, purity, open and reference-marking rules.
#ifndef EJK_FILESYSTEM_PAK_INDEX_H
#define EJK_FILESYSTEM_PAK_INDEX_H
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

template<class SearchPath> class PakProviderIndex {
    enum : uint32_t { End = UINT32_MAX };
    struct Member { SearchPath *provider; const char *name; uint32_t next, rank; };
    struct Directory { SearchPath *provider; uint32_t rank; };
    struct Storage {
        std::vector<Member> members;
        std::vector<Directory> directories;
        std::vector<uint32_t> buckets;
    };
    std::shared_ptr<Storage> storage;
    SearchPath *builtHead = nullptr;
    bool valid = false;
    bool unavailable = false;
    size_t builds = 0;
    double buildMs = 0;

    static unsigned char fold(unsigned char ch) {
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch == '\\' || ch == ':') ch = '/';
        return ch;
    }
    static uint32_t hash(const char *name) {
        uint32_t value = 2166136261U;
        while (*name) value = (value ^ fold(static_cast<unsigned char>(*name++))) * 16777619U;
        return value;
    }
    static bool same(const char *a, const char *b) {
        for (;;) {
            const unsigned char left = fold(static_cast<unsigned char>(*a++));
            const unsigned char right = fold(static_cast<unsigned char>(*b++));
            if (left != right) return false;
            if (!left) return true;
        }
    }
    bool rebuild(SearchPath *head) {
        const auto started = std::chrono::steady_clock::now();
        reset();
        builtHead = head;
        unavailable = true;
        // Bound the accelerator independently of the already-mounted assets.
        const size_t budget = (sizeof(void *) == 8 ? 64u : 32u) * size_t(1024 * 1024);
        size_t count = 0, directoryCount = 0;
        for (SearchPath *path = head; path; path = path->next) {
            if (path->dir) ++directoryCount;
            if (!path->pack) continue;
            for (int bucket = 0; bucket < path->pack->hashSize; ++bucket)
                for (auto *file = path->pack->hashTable[bucket]; file; file = file->next) {
                    if (++count > budget / (sizeof(Member) + 16)) return false;
                }
        }
        size_t bucketCount = 1;
        while (bucketCount < count * 2) bucketCount *= 2;
        if (count * sizeof(Member) + bucketCount * sizeof(uint32_t) + directoryCount * sizeof(Directory) > budget)
            return false;
        try {
            storage = std::make_shared<Storage>();
            storage->members.reserve(count);
            storage->directories.reserve(directoryCount);
            storage->buckets.assign(bucketCount, End);
            uint32_t rank = 0;
            for (SearchPath *path = head; path; path = path->next, ++rank) {
                if (path->dir) storage->directories.push_back({path, rank});
                if (!path->pack) continue;
                for (int bucket = 0; bucket < path->pack->hashSize; ++bucket)
                    for (auto *file = path->pack->hashTable[bucket]; file; file = file->next)
                        storage->members.push_back({path, file->name, End, rank});
            }
            // Reverse insertion leaves each bucket in search-path priority order.
            for (size_t i = storage->members.size(); i;) {
                Member &member = storage->members[--i];
                const size_t bucket = hash(member.name) & (storage->buckets.size() - 1);
                member.next = storage->buckets[bucket]; storage->buckets[bucket] = static_cast<uint32_t>(i);
            }
        } catch (...) {
            reset(); builtHead = head; unavailable = true;
            return false;
        }
        builtHead = head;
        valid = true;
        unavailable = false;
        ++builds;
        buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return true;
    }
public:
    void invalidate() { valid = unavailable = false; builtHead = nullptr; }
    void reset() {
        invalidate();
        storage.reset();
    }
    size_t buildCount() const { return builds; }
    size_t memberCount() const { return storage ? storage->members.size() : 0; }
    size_t memoryBytes() const {
        if (!storage) return 0;
        return storage->members.capacity() * sizeof(Member) + storage->directories.capacity() * sizeof(Directory) + storage->buckets.capacity() * sizeof(uint32_t);
    }
    double buildMilliseconds() const { return buildMs; }

    class Cursor {
        std::shared_ptr<const Storage> view;
        SearchPath *linear;
        const char *name;
        uint32_t member;
        size_t directory = 0;
    public:
        Cursor(std::shared_ptr<const Storage> owner, SearchPath *head, const char *filename)
            : view(std::move(owner)), linear(head), name(filename), member(view ? view->buckets[hash(filename) & (view->buckets.size() - 1)] : End) {}
        SearchPath *next() {
            if (!view) {
                SearchPath *result = linear;
                if (linear) linear = linear->next;
                return result;
            }
            while (member != End && !same(view->members[member].name, name)) member = view->members[member].next;
            if (directory < view->directories.size() &&
                (member == End || view->directories[directory].rank < view->members[member].rank))
                return view->directories[directory++].provider;
            if (member == End) return nullptr;
            SearchPath *result = view->members[member].provider;
            const uint32_t rank = view->members[member].rank;
            // The ordinary per-pak resolver checks the entire legacy bucket,
            // preserving duplicate entries and its separator/hash eligibility.
            do { member = view->members[member].next; }
            while (member != End && view->members[member].rank == rank);
            return result;
        }
    };
    Cursor lookup(SearchPath *head, const char *name, bool enabled = true) {
        if (!enabled || (unavailable && builtHead == head)) return Cursor(nullptr, head, name);
        if ((!valid || builtHead != head) && !rebuild(head)) return Cursor(nullptr, head, name);
        return Cursor(storage, head, name);
    }
};
#endif
