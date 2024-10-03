#include "test/jemalloc_test.h"

#include "jemalloc/internal/hist.h"

TEST_BEGIN(test_init) {
	hist_t hist;
	hist_init(&hist);

	for (int bin = 0; bin < HIST_NBINS; ++bin) {
		expect_d_eq(hist_get(&hist, bin), 0,
		    "Bin value should be zeroed after init");
	}
}
TEST_END

TEST_BEGIN(test_add) {
	hist_t hist;
	hist_init(&hist);

	hist_add(&hist, 0);
	hist_add(&hist, 0);
	hist_add(&hist, 1);
	hist_add(&hist, 2);
	hist_add(&hist, 3);
	hist_add(&hist, UINT64_MAX);

	/*
         * Histogram should look in the following way.
         * bin[0] = [0, 1]: 3, values (0, 0, 1).
         * bin[1] = [2, 4): 2, values (2, 3).
         * bin[2] = [4, 8): 0.
         * bin[3] = [8, 16): 0.
         * ....
         * bin[63] = [2**63, 2**64): 1, values: (UINT64_MAX).
	 */
	expect_d_eq(hist_get(&hist, 0), 3, "");
	expect_d_eq(hist_get(&hist, 1), 2, "");
	expect_d_eq(hist_get(&hist, 2), 0, "");
	expect_d_eq(hist_get(&hist, 3), 0, "");
	expect_d_eq(hist_get(&hist, fls_llu(UINT64_MAX)), 1, "");
}
TEST_END

TEST_BEGIN(test_remove) {
	hist_t hist;
	hist_init(&hist);

	hist_add(&hist, UINT64_MAX);
	hist_add(&hist, UINT64_MAX);
	hist_add(&hist, UINT64_MAX);

	hist_remove(&hist, UINT64_MAX);
	expect_d_eq(hist_get(&hist, fls_llu(UINT64_MAX)), 2, "");

	hist_remove(&hist, UINT64_MAX);
	hist_remove(&hist, UINT64_MAX);
	expect_d_eq(hist_get(&hist, fls_llu(UINT64_MAX)), 0, "");
}
TEST_END

TEST_BEGIN(test_merge) {
	hist_t dst;
	hist_init(&dst);

	hist_add(&dst, 0);
	hist_add(&dst, 2);

	hist_t src;
	hist_init(&src);

	hist_add(&src, 0);
	hist_add(&src, UINT64_MAX);

	hist_merge(&dst, &src);

	expect_d_eq(hist_get(&dst, 0), 2, "");
	expect_d_eq(hist_get(&dst, fls_llu(2)), 1, "");
	expect_d_eq(hist_get(&dst, fls_llu(UINT64_MAX)), 1, "");
}
TEST_END

TEST_BEGIN(test_print_bin_range) {
	char buf[HIST_RANGE_BUF_SIZE];

	hist_print_bin_range(0, buf);
	expect_str_eq("[0, 1]", buf, "");

	hist_print_bin_range(1, buf);
	expect_str_eq("[2, 4)", buf, "");

	hist_print_bin_range(2, buf);
	expect_str_eq("[4, 8)", buf, "");

	hist_print_bin_range(63, buf);
	expect_str_eq("[9223372036854775808, 18446744073709551615]", buf, "");
}
TEST_END

int
main(void) {
	return test(
		test_init,
		test_add,
		test_remove,
		test_merge,
		test_print_bin_range
	);
}
