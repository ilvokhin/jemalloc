#ifndef JEMALLOC_INTERNAL_PSSET_H
#define JEMALLOC_INTERNAL_PSSET_H

#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/hpdata.h"

/*
 * A page-slab set.  What the eset is to PAC, the psset is to HPA.  It maintains
 * a collection of page-slabs (the intent being that they are backed by
 * hugepages, or at least could be), and handles allocation and deallocation
 * requests.
 */

/*
 * One more than the maximum pszind_t we will serve out of the HPA.
 * Practically, we expect only the first few to be actually used.  This
 * corresponds to a maximum size of of 512MB on systems with 4k pages and
 * SC_NGROUP == 4, which is already an unreasonably large maximum.  Morally, you
 * can think of this as being SC_NPSIZES, but there's no sense in wasting that
 * much space in the arena, making bitmaps that much larger, etc.
 */
#define PSSET_NPSIZES 64

/*
 * We store non-hugefied and hugified pageslabs metadata separately.
 * [0] corresponds to non-hugified and [1] to hugified pageslabs.
 */
#define PSSET_NHUGE 2

typedef struct psset_bin_stats_s psset_bin_stats_t;
struct psset_bin_stats_s {
	/* How many pageslabs are in this bin? */
	size_t npageslabs;
	/* Of them, how many pages are active? */
	size_t nactive;
	/* And how many are dirty? */
	size_t ndirty;
};

typedef struct psset_stats_s psset_stats_t;
struct psset_stats_s {
	/*
	 * Merged stats for all pageslabs in psset.  This lets us quickly
	 * answer queries for the number of dirty and active pages in the
	 * entire set.
	 */
	psset_bin_stats_t merged;

	/*
	 * Below are the same stats, but aggregated by different
	 * properties of pageslabs: huginess or fullness.
	 */

	/* Non-huge and huge slabs. */
	psset_bin_stats_t slabs[PSSET_NHUGE];

	/* Non-full slabs, distinguished for non-huge and huge slabs. */
	psset_bin_stats_t nonfull_slabs[PSSET_NPSIZES][PSSET_NHUGE];

	/*
	 * Full slabs don't live in any edata heap, but we still track their
	 * stats.
	 */
	psset_bin_stats_t full_slabs[PSSET_NHUGE];

	/* Empty slabs are similar. */
	psset_bin_stats_t empty_slabs[PSSET_NHUGE];
};

typedef struct psset_s psset_t;
struct psset_s {
	/*
	 * The pageslabs, quantized by the size class of the largest contiguous
	 * free run of pages in a pageslab.
	 */
	hpdata_age_heap_t pageslabs[PSSET_NHUGE][PSSET_NPSIZES];
	/* Bitmap for which set bits correspond to non-empty heaps. */
	fb_group_t pageslab_bitmap[PSSET_NHUGE][FB_NGROUPS(PSSET_NPSIZES)];
	psset_stats_t stats;
	/*
	 * Slabs with no active allocations, but which are allowed to serve new
	 * allocations.
	 */
	hpdata_empty_list_t empty;
	/*
	 * Slabs which are available to be purged, ordered by how much we want
	 * to purge them (with later indices indicating slabs we want to purge
	 * more).
	 */
	hpdata_purge_list_t to_purge[PSSET_NHUGE][PSSET_NPSIZES];
	/* Bitmap for which set bits correspond to non-empty purge lists. */
	fb_group_t purge_bitmap[PSSET_NHUGE][FB_NGROUPS(PSSET_NPSIZES)];
	/* Slabs which are available to be hugified. */
	hpdata_hugify_list_t to_hugify;
};

void psset_init(psset_t *psset);
void psset_stats_accum(psset_stats_t *dst, psset_stats_t *src);

/*
 * Begin or end updating the given pageslab's metadata.  While the pageslab is
 * being updated, it won't be returned from psset_fit calls.
 */
void psset_update_begin(psset_t *psset, hpdata_t *ps);
void psset_update_end(psset_t *psset, hpdata_t *ps);

/* Analogous to the eset_fit; pick a hpdata to serve the request. */
hpdata_t *psset_pick_alloc(psset_t *psset, size_t size);
/* Pick one to purge. */
hpdata_t *psset_pick_purge(psset_t *psset);
/* Pick one to hugify. */
hpdata_t *psset_pick_hugify(psset_t *psset);

void psset_insert(psset_t *psset, hpdata_t *ps);
void psset_remove(psset_t *psset, hpdata_t *ps);

static inline size_t
psset_npageslabs(psset_t *psset) {
	return psset->stats.merged.npageslabs;
}

static inline size_t
psset_nactive(psset_t *psset) {
	return psset->stats.merged.nactive;
}

static inline size_t
psset_ndirty(psset_t *psset) {
	return psset->stats.merged.ndirty;
}

#endif /* JEMALLOC_INTERNAL_PSSET_H */
