#include "rendercheck/run_performance.h"

#include <cassert>

int main()
{
    using rendercheck::run_performance_exceeds_regression_floor;

    /* Relative noise on microsecond-scale metrics must not fail a run. */
    assert(!run_performance_exceeds_regression_floor(0.003, 0.002, 15.0, 0.05));

    /* A change below the configured relative threshold is not a regression. */
    assert(!run_performance_exceeds_regression_floor(1.14, 1.00, 15.0, 0.05));

    /* A material change must exceed both relative and absolute thresholds. */
    assert(run_performance_exceeds_regression_floor(1.20, 1.00, 15.0, 0.05));

    return 0;
}
