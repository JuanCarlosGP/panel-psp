#include <stdlib.h>

#include "stick_test.h"

enum {
    SETTLE_US = 650000,
    REST_US = STICK_REST_SECONDS * 1000000,
    RETURN_US = STICK_RETURN_SECONDS * 1000000,
    RETURN_SETTLE_US = 250000,
    DRIFT_US = 250000,
    EDGE_US = 30000,
    MAX_SAMPLE_GAP_US = 50000,
    CENTER_LIMIT_SQUARED = STICK_CENTER_TOLERANCE * STICK_CENTER_TOLERANCE,
};

bool stick_test_confirm(StickTest *test)
{
    switch (test->phase) {
    case STICK_READY:
    case STICK_RESULTS:
        *test = (StickTest){.phase = STICK_SETTLE, .edge_direction = -1};
        break;
    case STICK_RETURN_TARGET:
    case STICK_RETURN_RELEASE:
        test->phase = STICK_RETURN_OBSERVE;
        break;
    case STICK_RANGE_READY:
        test->phase = STICK_RANGE;
        break;
    case STICK_RANGE:
        test->phase = STICK_RESULTS;
        break;
    default:
        return false;
    }
    test->elapsed_us = 0;
    test->have_sample = false;
    return true;
}

bool stick_test_pause(StickTest *test)
{
    if (!test->have_sample)
        return false;
    test->have_sample = false;
    test->edge_direction = -1;
    test->edge_us = 0;
    test->elapsed_us = 0;
    if (test->phase == STICK_REST) {
        test->rest = (StickCenter){0};
        return true;
    }
    if (test->phase == STICK_RETURN_RELEASE || test->phase == STICK_RETURN_OBSERVE) {
        test->phase = STICK_RETURN_TARGET;
        test->return_peak = 0;
        test->after_release = (StickCenter){0};
        test->reached_center = false;
        return true;
    }
    return false;
}

static void measure_center(StickCenter *center, uint8_t x, uint8_t y,
                           unsigned int distance_squared, uint32_t elapsed)
{
    if (!center->samples) {
        center->min_x = center->max_x = x;
        center->min_y = center->max_y = y;
    }
    if (x < center->min_x)
        center->min_x = x;
    if (x > center->max_x)
        center->max_x = x;
    if (y < center->min_y)
        center->min_y = y;
    if (y > center->max_y)
        center->max_y = y;
    ++center->samples;
    center->sum_x += x;
    center->sum_y += y;
    if (distance_squared > center->peak_distance_squared)
        center->peak_distance_squared = distance_squared;

    bool outside = distance_squared > CENTER_LIMIT_SQUARED;
    center->outside_us = outside && center->was_outside ? center->outside_us + elapsed : 0;
    center->was_outside = outside;
    if (center->outside_us >= DRIFT_US)
        center->drift = true;
}

static enum StickDirection direction_for(int dx, int dy)
{
    int ax = abs(dx), ay = abs(dy);
    // Sector boundaries are 22.5 degrees from the horizontal or vertical axis.
    if (ay * 256 <= ax * 106)
        return dx < 0 ? STICK_W : STICK_E;
    if (ax * 256 <= ay * 106)
        return dy < 0 ? STICK_N : STICK_S;
    if (dy < 0)
        return dx < 0 ? STICK_NW : STICK_NE;
    return dx < 0 ? STICK_SW : STICK_SE;
}

static unsigned int axis_percent(int offset)
{
    int limit = offset < 0 ? STICK_MIDPOINT : UINT8_MAX - STICK_MIDPOINT;
    return (unsigned int)(abs(offset) * 100 / limit);
}

static int distance_along_axis(enum StickDirection direction, int dx, int dy)
{
    switch (direction) {
    case STICK_N: return -dy;
    case STICK_E: return dx;
    case STICK_S: return dy;
    case STICK_W: return -dx;
    default: return 0;
    }
}

static bool measure_range(StickTest *test, int dx, int dy, uint32_t elapsed)
{
    enum StickDirection direction = direction_for(dx, dy);
    unsigned int x_percent = axis_percent(dx), y_percent = axis_percent(dy);
    unsigned int percent;
    if (direction == STICK_N || direction == STICK_S)
        percent = y_percent;
    else if (direction == STICK_E || direction == STICK_W)
        percent = x_percent;
    else
        percent = x_percent < y_percent ? x_percent : y_percent;

    bool changed = false;
    if (percent > test->range_peak[direction]) {
        test->range_peak[direction] = (uint8_t)percent;
        changed = true;
    }
    if (percent < STICK_RANGE_TARGET) {
        test->edge_us = 0;
        test->edge_direction = -1;
        return changed;
    }

    if (test->edge_direction != (int)direction)
        test->edge_us = 0;
    else if (test->edge_us < EDGE_US)
        test->edge_us += elapsed;
    test->edge_direction = direction;
    if (test->edge_us >= EDGE_US && !(test->reached & (1u << direction))) {
        test->reached |= 1u << direction;
        changed = true;
    }
    return changed;
}

bool stick_test_sample(StickTest *test, uint32_t timestamp, uint8_t x, uint8_t y)
{
    if (test->phase <= STICK_READY || test->phase == STICK_RANGE_READY || test->phase == STICK_RESULTS)
        return false;

    uint32_t elapsed = test->have_sample ? timestamp - test->sample_time : 0;
    if (test->have_sample && (elapsed == 0 || elapsed > INT32_MAX))
        return false;
    bool changed = false;
    if (elapsed > MAX_SAMPLE_GAP_US) {
        changed = stick_test_pause(test);
        elapsed = 0;
    }
    test->sample_time = timestamp;
    test->have_sample = true;
    uint32_t previous = test->elapsed_us;

    int dx = (int)x - STICK_MIDPOINT;
    int dy = (int)y - STICK_MIDPOINT;
    unsigned int distance_squared = dx * dx + dy * dy;
    switch (test->phase) {
    case STICK_SETTLE:
        test->elapsed_us += elapsed;
        if (test->elapsed_us < SETTLE_US)
            return changed;
        test->phase = STICK_REST;
        test->elapsed_us = 0;
        measure_center(&test->rest, x, y, distance_squared, 0);
        return true;

    case STICK_REST:
        measure_center(&test->rest, x, y, distance_squared, elapsed);
        test->elapsed_us += elapsed;
        if (test->elapsed_us >= REST_US) {
            test->phase = STICK_RETURN_TARGET;
            test->elapsed_us = 0;
            return true;
        }
        return changed || previous / 50000 != test->elapsed_us / 50000;

    case STICK_RETURN_TARGET: {
        enum StickDirection direction = direction_for(dx, dy);
        int extent = distance_along_axis(direction, dx, dy);
        int axis_offset = direction == STICK_N || direction == STICK_S ? dy : dx;
        if (!extent || axis_percent(axis_offset) < STICK_RANGE_TARGET)
            return changed;
        test->return_direction = direction;
        test->return_peak = extent;
        test->phase = STICK_RETURN_RELEASE;
        return true;
    }

    case STICK_RETURN_RELEASE: {
        int extent = distance_along_axis(test->return_direction, dx, dy);
        if (extent > test->return_peak)
            test->return_peak = extent;
        if (test->return_peak - extent < STICK_CENTER_TOLERANCE)
            return changed;
        test->phase = STICK_RETURN_OBSERVE;
        test->elapsed_us = 0;
        test->reached_center = distance_squared <= CENTER_LIMIT_SQUARED;
        return true;
    }

    case STICK_RETURN_OBSERVE:
        test->reached_center |= distance_squared <= CENTER_LIMIT_SQUARED;
        test->elapsed_us += elapsed;
        // Measure jitter after the initial movement from the stop.
        if (test->elapsed_us >= RETURN_SETTLE_US)
            measure_center(&test->after_release, x, y, distance_squared, elapsed);
        if (test->elapsed_us >= RETURN_US) {
            test->phase = STICK_RANGE_READY;
            test->elapsed_us = 0;
            return true;
        }
        return changed || previous / 50000 != test->elapsed_us / 50000;

    case STICK_RANGE:
        if (distance_squared <= CENTER_LIMIT_SQUARED) {
            test->edge_direction = -1;
            test->edge_us = 0;
            return changed;
        }
        return measure_range(test, dx, dy, elapsed) || changed;

    default:
        return changed;
    }
}
