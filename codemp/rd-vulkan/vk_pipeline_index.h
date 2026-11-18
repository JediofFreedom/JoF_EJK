// Semantic pipeline keys avoid both linear searches and structure-padding keys.
#ifndef EJK_VK_PIPELINE_INDEX_H
#define EJK_VK_PIPELINE_INDEX_H
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

using VkPipelineKey = std::array<uint32_t, 16>;
struct VkPipelineKeyHash {
	std::size_t operator()(const VkPipelineKey &key) const {
		uint64_t value = UINT64_C(14695981039346656037);
		for (uint32_t word : key) { value ^= word; value *= UINT64_C(1099511628211); }
		return std::size_t(value ^ (value >> 32));
	}
};
class VkPipelineIndex {
	std::unordered_map<VkPipelineKey, std::vector<uint32_t>, VkPipelineKeyHash> entries;
public:
	void clear() { entries.clear(); }
	void add(const VkPipelineKey &key, uint32_t index) { entries[key].push_back(index); }
	bool find(const VkPipelineKey &key, uint32_t base, uint32_t &index) const {
		const auto entry = entries.find(key);
		if (entry == entries.end()) return false;
		const auto match = std::lower_bound(entry->second.begin(), entry->second.end(), base);
		if (match == entry->second.end()) return false;
		index = *match;
		return true;
	}
};
#endif
