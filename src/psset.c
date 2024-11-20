#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/psset.h"

#include "jemalloc/internal/fb.h"

static void
psset_init_pageslabs(hpdata_age_heap_t *pageslabs) {
	for (int i = 0; i < PSSET_NPSIZES; i++) {
		hpdata_age_heap_new(&pageslabs[i]);
	}
}

static void
psset_init_to_purge(hpdata_purge_list_t *to_purge) {
	for (int i = 0; i < PSSET_NPSIZES; i++) {
		hpdata_purge_list_init(&to_purge[i]);
	}
}

void
psset_init(psset_t *psset) {
	for (int huge = 0; huge < PSSET_NHUGE; huge++) {
		psset_init_pageslabs(psset->pageslabs[huge]);
		fb_init(psset->pageslab_bitmap[huge], PSSET_NPSIZES);
	}
	memset(&psset->stats, 0, sizeof(psset->stats));
	hpdata_empty_list_init(&psset->empty);
	for (int huge = 0; huge < PSSET_NHUGE; huge++) {
		psset_init_to_purge(psset->to_purge[huge]);
		fb_init(psset->purge_bitmap[huge], PSSET_NPSIZES);
	}
	hpdata_hugify_list_init(&psset->to_hugify);
}

static void
psset_bin_stats_accum(psset_bin_stats_t *dst, psset_bin_stats_t *src) {
	dst->npageslabs += src->npageslabs;
	dst->nactive += src->nactive;
	dst->ndirty += src->ndirty;
}

void
psset_stats_accum(psset_stats_t *dst, psset_stats_t *src) {
	psset_bin_stats_accum(&dst->merged, &src->merged);
	for (int huge = 0; huge < PSSET_NHUGE; huge++) {
		psset_bin_stats_accum(&dst->slabs[huge], &src->slabs[huge]);
		psset_bin_stats_accum(&dst->full_slabs[huge],
		    &src->full_slabs[huge]);
		psset_bin_stats_accum(&dst->empty_slabs[huge],
		    &src->empty_slabs[huge]);
	}
	for (pszind_t i = 0; i < PSSET_NPSIZES; i++) {
		psset_bin_stats_accum(&dst->nonfull_slabs[i][0],
		    &src->nonfull_slabs[i][0]);
		psset_bin_stats_accum(&dst->nonfull_slabs[i][1],
		    &src->nonfull_slabs[i][1]);
	}
}

static size_t
psset_hpdata_huge_index(const hpdata_t *ps) {
	return (size_t)hpdata_huge_get(ps);
}

/*
 * The stats maintenance strategy is to remove a pageslab's contribution to the
 * stats when we call psset_update_begin, and re-add it (to a potentially new
 * bin) when we call psset_update_end.
 */
JEMALLOC_ALWAYS_INLINE void
psset_slab_stats_insert_remove(psset_stats_t *stats,
    psset_bin_stats_t *binstats, hpdata_t *ps, bool insert) {
	size_t mul = insert ? (size_t)1 : (size_t)-1;
	size_t nactive = hpdata_nactive_get(ps);
	size_t ndirty = hpdata_ndirty_get(ps);

	stats->merged.npageslabs += mul * 1;
	stats->merged.nactive += mul * nactive;
	stats->merged.ndirty += mul * ndirty;

	/*
	 * Stats above are necessary for purging logic to work, everything
	 * below is to improve observability, thense is optional, so we don't
	 * update it, when stats disabled.
	 */
	if (!config_stats) {
		return;
	}

	size_t huge_idx = psset_hpdata_huge_index(ps);

	stats->slabs[huge_idx].npageslabs += mul * 1;
	stats->slabs[huge_idx].nactive += mul * nactive;
	stats->slabs[huge_idx].ndirty += mul * ndirty;

	binstats[huge_idx].npageslabs += mul * 1;
	binstats[huge_idx].nactive += mul * nactive;
	binstats[huge_idx].ndirty += mul * ndirty;

	if (config_debug) {
		psset_bin_stats_t check_stats[PSSET_NHUGE] = {{0}};
		for (int huge = 0; huge < PSSET_NHUGE; huge++) {
			psset_bin_stats_accum(&check_stats[huge],
			    &stats->full_slabs[huge]);
			psset_bin_stats_accum(&check_stats[huge],
			    &stats->empty_slabs[huge]);
			for (pszind_t pind = 0; pind < PSSET_NPSIZES; pind++) {
				psset_bin_stats_accum(&check_stats[huge],
				    &stats->nonfull_slabs[pind][huge]);
			}
		}

		assert(stats->merged.npageslabs
		    == check_stats[0].npageslabs + check_stats[1].npageslabs);
		assert(stats->merged.nactive
		    == check_stats[0].nactive + check_stats[1].nactive);
		assert(stats->merged.ndirty
		    == check_stats[0].ndirty + check_stats[1].ndirty);

		for (int huge = 0; huge < PSSET_NHUGE; huge++) {
			assert(stats->slabs[huge].npageslabs
			    == check_stats[huge].npageslabs);
			assert(stats->slabs[huge].nactive
			    == check_stats[huge].nactive);
			assert(stats->slabs[huge].ndirty
			    == check_stats[huge].ndirty);
		}
	}
}

static void
psset_slab_stats_insert(psset_stats_t *stats, psset_bin_stats_t *binstats,
    hpdata_t *ps) {
	psset_slab_stats_insert_remove(stats, binstats, ps, true);
}

static void
psset_slab_stats_remove(psset_stats_t *stats, psset_bin_stats_t *binstats,
    hpdata_t *ps) {
	psset_slab_stats_insert_remove(stats, binstats, ps, false);
}

static pszind_t
psset_hpdata_heap_index(const hpdata_t *ps) {
	assert(!hpdata_full(ps));
	assert(!hpdata_empty(ps));
	size_t longest_free_range = hpdata_longest_free_range_get(ps);
	pszind_t pind = sz_psz2ind(sz_psz_quantize_floor(
	    longest_free_range << LG_PAGE));
	assert(pind < PSSET_NPSIZES);
	return pind;
}

static void
psset_hpdata_heap_remove(psset_t *psset, hpdata_t *ps) {
	size_t huge_idx = psset_hpdata_huge_index(ps);
	pszind_t pind = psset_hpdata_heap_index(ps);
	hpdata_age_heap_t *heap = &psset->pageslabs[huge_idx][pind];
	hpdata_age_heap_remove(heap, ps);
	if (hpdata_age_heap_empty(heap)) {
		fb_unset(psset->pageslab_bitmap[huge_idx], PSSET_NPSIZES,
		    (size_t)pind);
	}
}

static void
psset_hpdata_heap_insert(psset_t *psset, hpdata_t *ps) {
	size_t huge_idx = psset_hpdata_huge_index(ps);
	pszind_t pind = psset_hpdata_heap_index(ps);
	hpdata_age_heap_t *heap = &psset->pageslabs[huge_idx][pind];
	if (hpdata_age_heap_empty(heap)) {
		fb_set(psset->pageslab_bitmap[huge_idx], PSSET_NPSIZES,
		    (size_t)pind);
	}
	hpdata_age_heap_insert(heap, ps);
}

static void
psset_stats_insert(psset_t *psset, hpdata_t *ps) {
	psset_stats_t *stats = &psset->stats;
	if (hpdata_empty(ps)) {
		psset_slab_stats_insert(stats, psset->stats.empty_slabs, ps);
	} else if (hpdata_full(ps)) {
		psset_slab_stats_insert(stats, psset->stats.full_slabs, ps);
	} else {
		pszind_t pind = psset_hpdata_heap_index(ps);
		psset_slab_stats_insert(stats, psset->stats.nonfull_slabs[pind],
		    ps);
	}
}

static void
psset_stats_remove(psset_t *psset, hpdata_t *ps) {
	psset_stats_t *stats = &psset->stats;
	if (hpdata_empty(ps)) {
		psset_slab_stats_remove(stats, psset->stats.empty_slabs, ps);
	} else if (hpdata_full(ps)) {
		psset_slab_stats_remove(stats, psset->stats.full_slabs, ps);
	} else {
		pszind_t pind = psset_hpdata_heap_index(ps);
		psset_slab_stats_remove(stats, psset->stats.nonfull_slabs[pind],
		    ps);
	}
}

/*
 * Put ps into some container so that it can be found during future allocation
 * requests.
 */
static void
psset_alloc_container_insert(psset_t *psset, hpdata_t *ps) {
	assert(!hpdata_in_psset_alloc_container_get(ps));
	hpdata_in_psset_alloc_container_set(ps, true);
	if (hpdata_empty(ps)) {
		/*
		 * This prepend, paired with popping the head in psset_fit,
		 * means we implement LIFO ordering for the empty slabs set,
		 * which seems reasonable.
		 */
		hpdata_empty_list_prepend(&psset->empty, ps);
	} else if (hpdata_full(ps)) {
		/*
		 * We don't need to keep track of the full slabs; we're never
		 * going to return them from a psset_pick_alloc call.
		 */
	} else {
		psset_hpdata_heap_insert(psset, ps);
	}
}

/* Remove ps from those collections. */
static void
psset_alloc_container_remove(psset_t *psset, hpdata_t *ps) {
	assert(hpdata_in_psset_alloc_container_get(ps));
	hpdata_in_psset_alloc_container_set(ps, false);

	if (hpdata_empty(ps)) {
		hpdata_empty_list_remove(&psset->empty, ps);
	} else if (hpdata_full(ps)) {
		/* Same as above -- do nothing in this case. */
	} else {
		psset_hpdata_heap_remove(psset, ps);
	}
}

static size_t
psset_purge_list_ind(hpdata_t *ps) {
	size_t ndirty = hpdata_ndirty_get(ps);
	/* Shouldn't have something with no dirty pages purgeable. */
	assert(ndirty > 0);
	/*
	 * Higher indices correspond to lists we'd like to purge earlier; make
	 * the highest index correspond to empty list, which we attempt
	 * to purge before purging any non-empty list.  This has two advantages:
	 * - Empty page slabs are the least likely to get reused (we'll only
	 *   pick them for an allocation if we have no other choice).
	 * - Empty page slabs can purge every dirty page they contain in a
	 *   single call, which is not usually the case.
	 */
	if (hpdata_nactive_get(ps) == 0) {
		return PSSET_NPSIZES - 1;
	}

	return sz_psz2ind(sz_psz_quantize_floor(ndirty << LG_PAGE));
}

static void
psset_maybe_remove_purge_list(psset_t *psset, hpdata_t *ps) {
	/*
	 * Remove the hpdata from its purge list (if it's in one).  Even if it's
	 * going to stay in the same one, by appending it during
	 * psset_update_end, we move it to the end of its queue, so that we
	 * purge LRU within a given dirtiness bucket.
	 */
	if (hpdata_purge_allowed_get(ps)) {
		size_t huge = psset_hpdata_huge_index(ps);
		size_t ind = psset_purge_list_ind(ps);
		hpdata_purge_list_t *purge_list = &psset->to_purge[huge][ind];
		hpdata_purge_list_remove(purge_list, ps);
		if (hpdata_purge_list_empty(purge_list)) {
			fb_unset(psset->purge_bitmap[huge], PSSET_NPSIZES,
			    ind);
		}
	}
}

static void
psset_maybe_insert_purge_list(psset_t *psset, hpdata_t *ps) {
	if (hpdata_purge_allowed_get(ps)) {
		size_t huge = psset_hpdata_huge_index(ps);
		size_t ind = psset_purge_list_ind(ps);
		hpdata_purge_list_t *purge_list = &psset->to_purge[huge][ind];
		if (hpdata_purge_list_empty(purge_list)) {
			fb_set(psset->purge_bitmap[huge], PSSET_NPSIZES, ind);
		}
		hpdata_purge_list_append(purge_list, ps);
	}

}

void
psset_update_begin(psset_t *psset, hpdata_t *ps) {
	hpdata_assert_consistent(ps);
	assert(hpdata_in_psset_get(ps));
	hpdata_updating_set(ps, true);
	psset_stats_remove(psset, ps);
	if (hpdata_in_psset_alloc_container_get(ps)) {
		/*
		 * Some metadata updates can break alloc container invariants
		 * (e.g. the longest free range determines the hpdata_heap_t the
		 * pageslab lives in).
		 */
		assert(hpdata_alloc_allowed_get(ps));
		psset_alloc_container_remove(psset, ps);
	}
	psset_maybe_remove_purge_list(psset, ps);
	/*
	 * We don't update presence in the hugify list; we try to keep it FIFO,
	 * even in the presence of other metadata updates.  We'll update
	 * presence at the end of the metadata update if necessary.
	 */
}

void
psset_update_end(psset_t *psset, hpdata_t *ps) {
	assert(hpdata_in_psset_get(ps));
	hpdata_updating_set(ps, false);
	psset_stats_insert(psset, ps);

	/*
	 * The update begin should have removed ps from whatever alloc container
	 * it was in.
	 */
	assert(!hpdata_in_psset_alloc_container_get(ps));
	if (hpdata_alloc_allowed_get(ps)) {
		psset_alloc_container_insert(psset, ps);
	}
	psset_maybe_insert_purge_list(psset, ps);

	if (hpdata_hugify_allowed_get(ps)
	    && !hpdata_in_psset_hugify_container_get(ps)) {
		hpdata_in_psset_hugify_container_set(ps, true);
		hpdata_hugify_list_append(&psset->to_hugify, ps);
	} else if (!hpdata_hugify_allowed_get(ps)
	    && hpdata_in_psset_hugify_container_get(ps)) {
		hpdata_in_psset_hugify_container_set(ps, false);
		hpdata_hugify_list_remove(&psset->to_hugify, ps);
	}
	hpdata_assert_consistent(ps);
}

hpdata_t *
psset_pick_alloc(psset_t *psset, size_t size) {
	assert((size & PAGE_MASK) == 0);
	assert(size <= HUGEPAGE);

	pszind_t min_pind = sz_psz2ind(sz_psz_quantize_ceil(size));

	/*
	 * Try to place allocation on already hugified page first if possible
	 * to better utilize them.
	 */
	for (int huge = PSSET_NHUGE - 1; huge >= 0; --huge) {
		pszind_t pind = (pszind_t)fb_ffs(psset->pageslab_bitmap[huge],
		    PSSET_NPSIZES, (size_t)min_pind);
		if (pind == PSSET_NPSIZES) {
			continue;
		}
		hpdata_t *ps = hpdata_age_heap_first(
		    &psset->pageslabs[huge][pind]);
		if (ps == NULL) {
			continue;
		}
		hpdata_assert_consistent(ps);
		return ps;
	}

	/*
	 * Couldn't find non-full slab to place allocation on, use empty slab
	 * if we have one available as last resort.
	 */
	return hpdata_empty_list_first(&psset->empty);
}

hpdata_t *
psset_pick_purge(psset_t *psset) {
	/*
	 * We purge hugeified empty slabs before nonhugeified ones, on the
	 * basis that they are fully dirty, while nonhugified slabs might not
	 * be, so we free up more pages more easily.  Another reason to prefer
	 * purging hugified slabs is to free continious physical memory ranges
	 * in case there is not enough of them due to fragmentation on
	 * operation system level.
	 */
	for (ssize_t huge = PSSET_NHUGE - 1; huge >= 0; --huge) {
		if (!fb_get(psset->purge_bitmap[huge], PSSET_NPSIZES,
		    PSSET_NPSIZES - 1)) {
			continue;
		}
		hpdata_t *ps = hpdata_purge_list_first(
		    &psset->to_purge[huge][PSSET_NPSIZES - 1]);
		assert(ps != NULL);
		return ps;
	}

	/* For non-empty pageslabs prioritize to purge non-hugified ones. */
	for (ssize_t huge = 0; huge < PSSET_NHUGE; ++huge) {
		ssize_t ind_ssz = fb_fls(psset->purge_bitmap[huge],
		    PSSET_NPSIZES, PSSET_NPSIZES - 1);
		if (ind_ssz < 0) {
			continue;
		}
		pszind_t ind = (pszind_t)ind_ssz;
		assert(ind < PSSET_NPSIZES);
		hpdata_t *ps = hpdata_purge_list_first(
		    &psset->to_purge[huge][ind]);
		assert(ps != NULL);
		return ps;
	}

	return NULL;
}

hpdata_t *
psset_pick_hugify(psset_t *psset) {
	return hpdata_hugify_list_first(&psset->to_hugify);
}

void
psset_insert(psset_t *psset, hpdata_t *ps) {
	hpdata_in_psset_set(ps, true);

	psset_stats_insert(psset, ps);
	if (hpdata_alloc_allowed_get(ps)) {
		psset_alloc_container_insert(psset, ps);
	}
	psset_maybe_insert_purge_list(psset, ps);

	if (hpdata_hugify_allowed_get(ps)) {
		hpdata_in_psset_hugify_container_set(ps, true);
		hpdata_hugify_list_append(&psset->to_hugify, ps);
	}
}

void
psset_remove(psset_t *psset, hpdata_t *ps) {
	hpdata_in_psset_set(ps, false);

	psset_stats_remove(psset, ps);
	if (hpdata_in_psset_alloc_container_get(ps)) {
		psset_alloc_container_remove(psset, ps);
	}
	psset_maybe_remove_purge_list(psset, ps);
	if (hpdata_in_psset_hugify_container_get(ps)) {
		hpdata_in_psset_hugify_container_set(ps, false);
		hpdata_hugify_list_remove(&psset->to_hugify, ps);
	}
}
