#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/hist.h"

static int
hist_bin_ind(uint64_t val) {
	if (val == 0) {
		return 0;
	}
	return fls_llu(val);
}

void
hist_init(hist_t *hist) {
	memset(hist->bins, 0, HIST_NBINS * sizeof(uint64_t));
}

void
hist_add(hist_t *hist, uint64_t val) {
	int bin = hist_bin_ind(val);
	++hist->bins[bin];
}

void
hist_remove(hist_t *hist, uint64_t val) {
	int bin = hist_bin_ind(val);
	assert(hist->bins[bin] > 0);
	--hist->bins[bin];
}

uint64_t
hist_get(hist_t *hist, uint64_t bin) {
	assert(bin < HIST_NBINS);
	return hist->bins[bin];
}

void
hist_merge(hist_t *dst, const hist_t *src) {
	for (int bin = 0; bin < HIST_NBINS; ++bin) {
		dst->bins[bin] += src->bins[bin];
	}
}

void
hist_print_bin_range(uint64_t bin, char buf[HIST_RANGE_BUF_SIZE]) {
	assert(bin < HIST_NBINS);
	/*
	 * Special treatment for zero bin as it's range doesn't fall into
	 * common logic.
	 */
	if (bin == 0) {
		malloc_snprintf(buf, HIST_RANGE_BUF_SIZE, "[0, 1]");
		return;
	}
	uint64_t bin_beg = (1ULL << bin);
	size_t printed = malloc_snprintf(buf, HIST_RANGE_BUF_SIZE,
	    "[%"FMTu64", ", bin_beg);
	/* Last bin includes right boundary as it is UINT64_MAX. */
	if (bin == HIST_NBINS - 1) {
		malloc_snprintf(&buf[printed], HIST_RANGE_BUF_SIZE - printed,
		    "%"FMTu64"]", UINT64_MAX);
		return;
	}
	uint64_t bin_end = (1ULL << (bin + 1));
	malloc_snprintf(&buf[printed], HIST_RANGE_BUF_SIZE - printed,
	    "%"FMTu64")", bin_end);
}
