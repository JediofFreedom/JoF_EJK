// Bounded ownership of immutable GPU resources across map/hunk resets.
#ifndef EJK_VK_RESIDENT_CACHE_H
#define EJK_VK_RESIDENT_CACHE_H
#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

template<class Resource> class ResidentResourceCache {
public:
	using Pointer = std::shared_ptr<Resource>;
	size_t hits = 0, misses = 0, evictions = 0;
	size_t bytes() const { return used; }
	size_t limit() const { return budget; }
	size_t size() const { return index.size(); }
	void setLimit(size_t value) {
		budget = value;
		while (used > budget) evict();
	}
	Pointer find(const std::string &key, const std::vector<unsigned char> &source) {
		auto found = index.find(key);
		if (found == index.end() || found->second->source != source) { ++misses; return {}; }
		auto node = found->second;
		if (node->hot) hot.splice(hot.begin(), hot, node);
		else { node->hot = true; hot.splice(hot.begin(), cold, node); }
		++hits;
		return node->resource;
	}
	void store(const std::string &key, std::vector<unsigned char> source, Pointer resource, size_t gpuBytes) {
		if (gpuBytes > budget || source.size() > budget - gpuBytes) return;
		const size_t total = gpuBytes + source.size();
		erase(key);
		while (used > budget - total) evict();
		cold.push_front({key, std::move(source), std::move(resource), total, false});
		index.emplace(key, cold.begin());
		used += total;
	}
	void erase(const std::string &key, const Pointer &expected = {}) {
		auto found = index.find(key);
		if (found != index.end() && (!expected || found->second->resource == expected)) remove(found->second);
	}
	// Allocation pressure may reclaim only entries no current map still owns.
	bool evictIdle() {
		for (auto *list : {&cold, &hot}) {
			for (auto it = list->end(); it != list->begin();) {
				--it;
				if (it->resource.use_count() == 1) { remove(it); ++evictions; return true; }
			}
		}
		return false;
	}
	void clear() { index.clear(); cold.clear(); hot.clear(); used = 0; }
private:
	struct Entry {
		std::string key;
		std::vector<unsigned char> source;
		Pointer resource;
		size_t bytes;
		bool hot;
	};
	using List = std::list<Entry>;
	List cold, hot;
	std::unordered_map<std::string, typename List::iterator> index;
	size_t used = 0, budget = 0;
	void remove(typename List::iterator it) {
		used -= it->bytes;
		index.erase(it->key);
		if (it->hot) hot.erase(it); else cold.erase(it);
	}
	void evict() {
		List &list = cold.empty() ? hot : cold;
		remove(--list.end()); ++evictions;
	}
};
#endif
