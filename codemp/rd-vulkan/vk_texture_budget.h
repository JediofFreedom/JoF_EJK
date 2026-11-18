// Retained texture memory scales with device capacity and driver headroom.
#ifndef EJK_VK_TEXTURE_BUDGET_H
#define EJK_VK_TEXTURE_BUDGET_H
#include <algorithm>
#include <cstddef>
#include <cstdint>

static size_t VK_TextureCacheBudget(int requestedMegs, bool wideProcess, uint64_t localHeap,
	bool hasBudget = false, uint64_t heapBudget = 0, uint64_t heapUsage = 0, uint64_t retainedPages = 0) {
	const uint64_t mib = UINT64_C(1024) * 1024;
	const uint64_t maximum = 2048 * mib;
	const uint64_t requested = requestedMegs < 0 ?
		(wideProcess ? std::min(localHeap / 8, maximum) : 128 * mib) :
		std::min<uint64_t>(uint64_t(requestedMegs), 2048) * mib;
	uint64_t limit = std::min(requested, localHeap / 4);
	if (hasBudget) {
		// Existing retained pages are already included in the driver's usage.
		// Keep half the remaining allowance for fresh map/rendering allocations.
		const uint64_t otherUsage = heapUsage > retainedPages ? heapUsage - retainedPages : 0;
		const uint64_t available = heapBudget > otherUsage ? (heapBudget - otherUsage) / 2 : 0;
		limit = std::min(limit, available);
	}
	// The allocator uses 32 MiB pages, so smaller limits cannot retain images.
	return limit >= 32 * mib ? size_t(limit) : 0;
}
#endif
