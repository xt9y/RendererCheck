#include "rendercheck/run_performance.h"

#include <cassert>

int main()
{
    using rendercheck::RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS;
    using rendercheck::run_performance_exceeds_regression_floor;

    static_assert(RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS == 0.25);

    /* Scheduler noise observed in TemporalMotion: the relative increase is
     * above 15%, but the absolute change is only 0.149 ms. */
    assert(!run_performance_exceeds_regression_floor(1.076, 0.927, 15.0));

    /* Relative threshold still protects larger timings. */
    assert(!run_performance_exceeds_regression_floor(11.4, 10.0, 15.0));
    assert(run_performance_exceeds_regression_floor(11.6, 10.0, 15.0));

    /* Small timers must clear the 0.25 ms absolute floor as well. */
    assert(!run_performance_exceeds_regression_floor(1.20, 1.00, 15.0));
    assert(run_performance_exceeds_regression_floor(1.30, 1.00, 15.0));

    return 0;
}
