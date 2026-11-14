#include "button_test.h"

enum {
    HOLD_US = BUTTON_HOLD_SECONDS * 1000000,
    RESET_HOLD_US = 650000,
    MAX_SAMPLE_GAP_US = 25000,
};

bool button_test_sample(ButtonTest *test, Panel *panel, uint32_t timestamp, unsigned int buttons)
{
    uint32_t elapsed = test->have_sample ? timestamp - test->sample_time : 0;
    if (test->have_sample && (elapsed == 0 || elapsed > INT32_MAX))
        return false; // Already processed, including older samples returned by peek.
    test->sample_time = timestamp;
    test->have_sample = true;

    unsigned int previous = panel->pressed;
    if (elapsed > MAX_SAMPLE_GAP_US) {
        // A missing stretch of input cannot count as continuous pressure.
        previous = 0;
        test->reset_us = 0;
    }

    const unsigned int reset_buttons = PSP_CTRL_SELECT | PSP_CTRL_START;
    unsigned int pressed = buttons & PANEL_BUTTONS;
    bool clear_results = false;
    if (!(pressed & reset_buttons))
        test->wait_reset_release = false;
    if (!test->wait_reset_release && (pressed & reset_buttons) == reset_buttons) {
        test->reset_us = (previous & reset_buttons) == reset_buttons ? test->reset_us + elapsed : 0;
        if (test->reset_us >= RESET_HOLD_US) {
            clear_results = true;
            test->wait_reset_release = true;
            previous = 0;
        }
    } else {
        test->reset_us = 0;
    }
    if (test->wait_reset_release)
        pressed &= ~reset_buttons;

    unsigned int complete = 0;
    uint32_t longest = 0;
    for (unsigned int remaining = pressed; remaining; remaining &= remaining - 1) {
        unsigned int index = (unsigned int)__builtin_ctz(remaining);
        unsigned int button = 1u << index;
        uint32_t duration = (previous & button) ? test->held_us[index] + elapsed : 0;
        if (duration >= HOLD_US) {
            duration = HOLD_US;
            complete |= button;
        }
        if (duration > longest)
            longest = duration;
        test->held_us[index] = duration;
    }

    // Each button has its own timer. The shared bar shows the longest current hold.
    unsigned int progress = longest * 100 / HOLD_US;
    unsigned int tested = (clear_results ? 0 : panel->tested) | pressed;
    unsigned int validated = (clear_results ? 0 : panel->validated) | complete;
    bool changed = panel->pressed != pressed || panel->tested != tested ||
                   panel->validated != validated || panel->holding_complete != complete ||
                   panel->hold_percent != progress;
    panel->pressed = pressed;
    panel->tested = tested;
    panel->validated = validated;
    panel->holding_complete = complete;
    panel->hold_percent = progress;
    return changed;
}

bool button_test_pause(ButtonTest *test, Panel *panel)
{
    bool changed = panel->pressed != 0 || panel->hold_percent != 0;
    test->have_sample = false;
    test->reset_us = 0;
    panel->pressed = 0;
    panel->holding_complete = 0;
    panel->hold_percent = 0;
    return changed;
}
