#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/demand.h"

void
demand_init(demand_t *demand, uint64_t interval_ms) {
	assert(interval_ms > 0);
	demand->epoch = 0;
	uint64_t interval_ns = interval_ms * 1000 * 1000;
	demand->epoch_interval_ns = interval_ns / DEMAND_NBUCKETS;
	memset(demand->nactive_max, 0, sizeof(demand->nactive_max));
}

static uint64_t
demand_epoch_ind(demand_t *demand) {
	return demand->epoch % DEMAND_NBUCKETS;
}

static nstime_t
demand_next_epoch_advance(demand_t *demand) {
	uint64_t ns = (demand->epoch + 1) * demand->epoch_interval_ns;
	nstime_t next;
	nstime_init(&next, ns);
	return next;
}

static void
demand_maybe_advance_epoch(demand_t *demand, const nstime_t *now) {
	nstime_t next_epoch_advance = demand_next_epoch_advance(demand);
	if (nstime_compare(now, &next_epoch_advance) < 0) {
		return;
	}

	uint64_t next_epoch = nstime_ns(now) / demand->epoch_interval_ns;
	assert(next_epoch > demand->epoch);
	uint64_t delta = next_epoch - demand->epoch;

	/*
	 * If delta is greater than DEMAND_NBUCKETS, we don't want to do extra
	 * work and re-write same item in nactive_max multiple times, we'd like
	 * to do it at most once.
	 */
	if (delta > DEMAND_NBUCKETS) {
		delta = DEMAND_NBUCKETS;
	}
	while (delta-- > 0) {
		++demand->epoch;
		uint64_t ind = demand_epoch_ind(demand);
		demand->nactive_max[ind] = 0;
	}
	demand->epoch = next_epoch;
	return;
}

void
demand_update(demand_t *demand, const nstime_t *now, size_t nactive) {
	demand_maybe_advance_epoch(demand, now);
	uint64_t ind = demand_epoch_ind(demand);
	size_t *epoch_nactive = &demand->nactive_max[ind];
	if (nactive > *epoch_nactive) {
		*epoch_nactive = nactive;
	}
}

size_t
demand_nactive_max(demand_t *demand) {
	size_t nactive_max = demand->nactive_max[0];
	for (int i = 1; i < DEMAND_NBUCKETS; ++i) {
		if (demand->nactive_max[i] > nactive_max) {
			nactive_max = demand->nactive_max[i];
		}
	}
	return nactive_max;
}
