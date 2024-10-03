#ifndef JEMALLOC_INTERNAL_HIST_H
#define JEMALLOC_INTERNAL_HIST_H

#define HIST_NBINS 64

/*
 * Histogram range looks in the following way: [a, b). Both a and b can be up
 * to UINT64_MAX. In addition, we have four punctuation characters for prettier
 * formatting: '[', ',', ' ' and ')' and one more byte is required for null
 * character.
 */
#define HIST_RANGE_MAX_VALUE_TEXT_LEN 20
#define HIST_RANGE_FMT_VALUE_LEN 4
#define HIST_RANGE_BUF_SIZE \
    (2 * HIST_RANGE_MAX_VALUE_TEXT_LEN + HIST_RANGE_FMT_VALUE_LEN + 1)

/*
 * Implementation if histogram with power of 2 bins.
 *
 * Values 0 and 1 goes to bin[0] for implementation convenience, other values
 * bin is determined on position of most significant bit. Any uint64_t is
 * acceptable value to put into histogram.
 *
 * Example of bins are following.
 * bin[0]: [0, 1],
 * bin[1]: [2, 4),
 * bin[2]: [4, 8),
 * bin[3]: [16, 32),
 * ...
 * bin[63]: [2**63, 2**64).
 */
typedef struct hist_s hist_t;
struct hist_s {
	uint64_t bins[HIST_NBINS];
};

void hist_init(hist_t *hist);

void hist_add(hist_t *hist, uint64_t val);

void hist_remove(hist_t *hist, uint64_t val);

uint64_t hist_get(hist_t *hist, uint64_t bin);

void hist_merge(hist_t *dst, const hist_t *src);

void hist_print_bin_range(uint64_t bin, char buf[HIST_RANGE_BUF_SIZE]);

#endif /* JEMALLOC_INTERNAL_HIST_H */
