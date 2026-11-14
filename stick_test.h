#ifndef PANEL_STICK_TEST_H
#define PANEL_STICK_TEST_H

#include <stdbool.h>
#include <stdint.h>

enum StickPhase {
    STICK_CLOSED,
    STICK_READY,
    STICK_SETTLE,
    STICK_REST,
    STICK_RETURN_TARGET,
    STICK_RETURN_RELEASE,
    STICK_RETURN_OBSERVE,
    STICK_RANGE_READY,
    STICK_RANGE,
    STICK_RESULTS,
};

enum StickDirection {
    STICK_N, STICK_NE, STICK_E, STICK_SE, STICK_S, STICK_SW, STICK_W, STICK_NW,
    STICK_DIRECTIONS,
};

enum {
    STICK_MIDPOINT = 128,
    STICK_REST_SECONDS = 5,
    STICK_RETURN_SECONDS = 2,
    STICK_CENTER_TOLERANCE = 12,
    STICK_JITTER_TOLERANCE = 6,
    STICK_RANGE_TARGET = 85,
};

typedef struct {
    uint32_t samples;
    uint32_t sum_x, sum_y;
    uint8_t min_x, max_x, min_y, max_y;
    unsigned int peak_distance_squared;
    uint32_t outside_us;
    bool was_outside;
    bool drift;
} StickCenter;

typedef struct {
    enum StickPhase phase;
    uint32_t sample_time;
    uint32_t elapsed_us;
    bool have_sample;
    StickCenter rest;
    StickCenter after_release;
    enum StickDirection return_direction;
    int return_peak;
    bool reached_center;
    uint8_t range_peak[STICK_DIRECTIONS];
    unsigned int reached;
    int edge_direction;
    uint32_t edge_us;
} StickTest;

bool stick_test_confirm(StickTest *test);
bool stick_test_pause(StickTest *test);
bool stick_test_sample(StickTest *test, uint32_t timestamp, uint8_t x, uint8_t y);

#endif
