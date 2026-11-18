#ifndef EJK_VK_ALLOCATION_RANGES_H
#define EJK_VK_ALLOCATION_RANGES_H
#include <cstddef>
#include <map>

class AllocationRanges {
public:
	explicit AllocationRanges(size_t capacity) { free[0] = capacity; }
	bool allocate(size_t bytes, size_t alignment, size_t &offset) {
		if (!bytes || !alignment) return false;
		for (auto it = free.begin(); it != free.end(); ++it) {
			const size_t start = it->first, length = it->second;
			const size_t remainder = start % alignment;
			const size_t padding = remainder ? alignment - remainder : 0;
			if (padding > length || bytes > length - padding) continue;
			offset = start + padding;
			free.erase(it);
			if (padding) free[start] = padding;
			if (bytes < length - padding) free[offset + bytes] = length - padding - bytes;
			return true;
		}
		return false;
	}
	void release(size_t offset, size_t bytes) {
		auto next = free.lower_bound(offset);
		if (next != free.begin()) {
			auto previous = next; --previous;
			if (previous->first + previous->second == offset) {
				offset = previous->first; bytes += previous->second; free.erase(previous);
			}
		}
		if (next != free.end() && offset + bytes == next->first) {
			bytes += next->second; free.erase(next);
		}
		free[offset] = bytes;
	}
private:
	std::map<size_t, size_t> free;
};
#endif
