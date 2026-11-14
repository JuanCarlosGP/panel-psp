#ifndef PANEL_PSP_H
#define PANEL_PSP_H

#include <stdint.h>
#include <pspctrl.h>
#include "language.h"
#include "stick_test.h"

enum { PANEL_WIDTH = 480, PANEL_HEIGHT = 272, PANEL_STRIDE = 512, PANEL_VOLUME_MAX = 30 };

#define PANEL_BUTTONS (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT | \
                       PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE | PSP_CTRL_CROSS | PSP_CTRL_SQUARE | \
                       PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_SELECT | PSP_CTRL_START | \
                       PSP_CTRL_VOLDOWN | PSP_CTRL_VOLUP)

enum BatteryStatus {
    BATTERY_UNAVAILABLE,
    BATTERY_ABSENT,
    BATTERY_CHARGING,
    BATTERY_PLUGGED_IN,
    BATTERY_DISCHARGING,
};

typedef struct {
    enum PanelLanguage language;
    bool language_unsaved;
    int battery_percent;
    enum BatteryStatus battery_status;
    uint64_t free_bytes;
    uint64_t total_bytes;
    unsigned int pressed;
    unsigned int tested;
    unsigned int validated;
    unsigned int holding_complete;
    unsigned int hold_percent;
    uint8_t stick_x;
    uint8_t stick_y;
    int volume;
    int volume_muted;
    int wlan_switch;
} Panel;

void panel_draw(uint32_t *pixels, const Panel *panel);
void stick_test_draw(uint32_t *pixels, const Panel *panel, const StickTest *test);

#endif
