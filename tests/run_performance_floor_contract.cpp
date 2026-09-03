#include "rendercheck/run_performance.h"

#include <cassert>

int main()
{
    using rendercheck::RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS;
    using rendercheck::RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS;
    using rendercheck::RUN_PERFORMANCE_P95_MIN_SAMPLES;
    using rendercheck::run_performance_exceeds_regression_floor;
    using rendercheck::run_performance_p95_is_gating;

    static_assert(RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS == 0.25);
    static_assert(RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS == 0.50);
    static_assert(RUN_PERFORMANCE_P95_MIN_SAMPLES == 40u);

    assert(!run_performance_p95_is_gating(0u));
    assert(!run_performance_p95_is_gating(10u));
    assert(!run_performance_p95_is_gating(24u));
    assert(!run_performance_p95_is_gating(39u));
    assert(run_performance_p95_is_gating(40u));
    assert(run_performance_p95_is_gating(48u));

    /* Median scheduler noise remains protected by the stricter 0.25 ms floor. */
    assert(!run_performance_exceeds_regression_floor(1.076, 0.927, 15.0));
    assert(run_performance_exceeds_regression_floor(1.30, 1.00, 15.0));

    /* The p95 floor remains useful once the sample count is large enough for
     * p95 to be a meaningful tail statistic. */
    assert(!run_performance_exceeds_regression_floor(
            2.403, 1.993, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));
    assert(run_performance_exceeds_regression_floor(
            2.60, 1.99, 15.0, RUN_PERFORMANCE_P95_ABSOLUTE_FLOOR_MS));

    /* Relative threshold still protects larger timings. */
    assert(!run_performance_exceeds_regression_floor(11.4, 10.0, 15.0));
    assert(run_performance_exceeds_regression_floor(11.6, 10.0, 15.0));

    return 0;
}
