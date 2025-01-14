#include "test/jemalloc_test.h"

#include "jemalloc/internal/demand.h"

TEST_BEGIN(test_demand_init) {
	demand_t demand;
	/*
	 * Exact value doesn't matter here as we don't advance epoch in this
	 * test.
	 */
	uint64_t interval_ms = 1000;
	demand_init(&demand, interval_ms);

	expect_zu_eq(demand_nactive_max(&demand), 0,
	    "Unexpected ndirty_max value after initialization");
}
TEST_END

TEST_BEGIN(test_demand_update_basic) {
	demand_t demand;
	/* Make each bucket exactly one second to simplify math. */
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;

	nstime_init2(&now, /* sec */ 0, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 1024);

	nstime_init2(&now, /* sec */ 1, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 512);

	nstime_init2(&now, /* sec */ 2, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 256);

	expect_zu_eq(demand_nactive_max(&demand), 1024, "");
}
TEST_END

TEST_BEGIN(test_demand_update_skip_epochs) {
	demand_t demand;
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;

	nstime_init2(&now, /* sec */ 0, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 1024);

	nstime_init2(&now, /* sec */ DEMAND_NBUCKETS - 1, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 512);

	nstime_init2(&now, /* sec */ 2 * (DEMAND_NBUCKETS - 1), /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 256);

	/*
	 * Updates are not evenly spread over time.  When we update at
	 * 2 * (DEMAND_NBUCKETS - 1) second, 1024 value is already out of
	 * sliding window, but 512 is still present.
	 */
	expect_zu_eq(demand_nactive_max(&demand), 512, "");
}
TEST_END

TEST_BEGIN(test_demand_update_rewrite_opt) {
	demand_t demand;
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;

	nstime_init2(&now, /* sec */ 0, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 1024);

	nstime_init2(&now, /* sec */ 0, /* nsec */ UINT64_MAX);
	/*
	 * This update should take reasonable time if optimization is working
	 * correctly, otherwise we'll loop from 0 to UINT64_MAX and this test
	 * will take a long time to finish.
	 */
	demand_update(&demand, &now, /* nactive */ 512);

	expect_zu_eq(demand_nactive_max(&demand), 512, "");
}
TEST_END

TEST_BEGIN(test_demand_update_out_of_interval) {
	demand_t demand;
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;

	nstime_init2(&now, /* sec */ 0 * DEMAND_NBUCKETS, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 1024);

	nstime_init2(&now, /* sec */ 1 * DEMAND_NBUCKETS, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 512);

	nstime_init2(&now, /* sec */ 2 * DEMAND_NBUCKETS, /* nsec */ 0);
	demand_update(&demand, &now, /* nactive */ 256);

	/*
	 * Updates frequency is lower than tracking interval, so we should
	 * have only last value.
	 */
	expect_zu_eq(demand_nactive_max(&demand), 256, "");
}
TEST_END

TEST_BEGIN(test_demand_update_static_epoch) {
	demand_t demand;
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;
	nstime_init_zero(&now);

	/* Big enough value to overwrite values in circular buffer. */
	size_t nactive_max = 2 * DEMAND_NBUCKETS;
	for (size_t nactive = 0; nactive <= nactive_max; ++nactive) {
		/*
		 * We should override value in the same bucket as now value
		 * doesn't change between iterations.
		 */
		demand_update(&demand, &now, nactive);
	}

	expect_zu_eq(demand_nactive_max(&demand), nactive_max, "");
}
TEST_END

TEST_BEGIN(test_demand_update_epoch_advance) {
	demand_t demand;
	uint64_t interval_ms = 1000 * DEMAND_NBUCKETS;
	demand_init(&demand, interval_ms);

	nstime_t now;
	/* Big enough value to overwrite values in circular buffer. */
	size_t nactive_max = 2 * DEMAND_NBUCKETS;
	for (size_t nactive = 0; nactive <= nactive_max; ++nactive) {
		uint64_t sec = nactive;
		nstime_init2(&now, sec, /* nsec */ 0);
		demand_update(&demand, &now, nactive);
	}

	expect_zu_eq(demand_nactive_max(&demand), nactive_max, "");
}
TEST_END

int
main(void) {
	return test_no_reentrancy(
	    test_demand_init,
	    test_demand_update_basic,
	    test_demand_update_skip_epochs,
	    test_demand_update_rewrite_opt,
	    test_demand_update_out_of_interval,
	    test_demand_update_static_epoch,
	    test_demand_update_epoch_advance);
}
