#ifndef PANEL_BUTTON_TEST_H
#define PANEL_BUTTON_TEST_H

#include <stdbool.h>
#include "panel.h"

enum { BUTTON_HOLD_SECONDS = 5 };

typedef struct {
    uint32_t held_us[32];
    uint32_t sample_time;
    uint32_t reset_us;
    bool have_sample;
    bool wait_reset_release;
} ButtonTest;

bool button_test_sample(ButtonTest *test, Panel *panel, uint32_t timestamp, unsigned int buttons);
bool button_test_pause(ButtonTest *test, Panel *panel);

#endif
