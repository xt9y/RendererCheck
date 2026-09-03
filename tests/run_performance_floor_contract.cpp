#include "rendercheck/run_performance.h"

#include <cassert>

int main()
{
    using rendercheck::RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS;
    using rendercheck::RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS;
    using rendercheck::run_performance_exceeds_regression_floor;

    static_assert(RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS == 0.25);
    static_assert(RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS == 0.50);

    /* Median scheduler noise remains protected by the stricter 0.25 ms floor. */
    assert(!run_performance_exceeds_regression_floor(1.076, 0.927, 15.0));
    assert(run_performance_exceeds_regression_floor(1.30, 1.00, 15.0));

    /* Short-test p95 samples need a wider floor. These are the exact scale of
     * tail spikes observed while the end-to-end frame medians stayed stable. */
    assert(!run_performance_exceeds_regression_floor(
            2.270, 1.967, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));
    assert(!run_performance_exceeds_regression_floor(
            2.403, 1.993, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));
    assert(!run_performance_exceeds_regression_floor(
            1.215, 0.929, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));

    /* Material tail regressions still exceed both thresholds. */
    assert(run_performance_exceeds_regression_floor(
            2.60, 1.99, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));

    /* Relative threshold still protects larger timings. */
    assert(!run_performance_exceeds_regression_floor(11.4, 10.0, 15.0));
    assert(run_performance_exceeds_regression_floor(11.6, 10.0, 15.0));

    return 0;
}
