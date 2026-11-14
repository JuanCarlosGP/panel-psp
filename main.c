#include <pspuser.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <psppower.h>
#include <psputility_sysparam.h>
#include <pspwlan.h>
#include <pspimpose_driver.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <stdbool.h>

#include "panel.h"
#include "button_test.h"

PSP_MODULE_INFO("Panel PSP", PSP_MODULE_USER, 1, 1);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);

enum {
    MEMORY_STICK_CAPACITY = 0x02425818,
    BATTERY_REFRESH_US = 1000000,
    STORAGE_REFRESH_US = 15000000,
    SYSTEM_REFRESH_US = 100000,
    CONTROLLER_SAMPLE_US = 5555,
    CONTROLLER_HISTORY = 32,
    BUFFER_PIXELS = PANEL_STRIDE * PANEL_HEIGHT,
    BUFFER_BYTES = BUFFER_PIXELS * sizeof(uint32_t),
};

typedef struct {
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint32_t reserved;
    uint32_t sector_size;
    uint32_t sectors_per_cluster;
} StickCapacity;

static int exit_callback(int arg1, int arg2, void *context)
{
    (void)arg1;
    (void)arg2;
    (void)context;
    sceKernelExitGame();
    return 0;
}

static int exit_thread(SceSize argc, void *argv)
{
    (void)argc;
    (void)argv;
    SceUID callback = sceKernelCreateCallback("PanelExit", exit_callback, NULL);
    if (callback < 0)
        return callback;
    if (sceKernelRegisterExitCallback(callback) < 0)
        return sceKernelDeleteCallback(callback);
    return sceKernelSleepThreadCB();
}

static enum PanelLanguage system_language(void)
{
    int language;
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &language) == 0 &&
        language != PSP_SYSTEMPARAM_LANGUAGE_SPANISH)
        return PANEL_ENGLISH;
    return PANEL_SPANISH;
}

static void read_battery(Panel *panel)
{
    int present = scePowerIsBatteryExist();
    int percent = present > 0 ? scePowerGetBatteryLifePercent() : -1;
    panel->battery_percent = (percent >= 0 && percent <= 100) ? percent : -1;

    if (present == 0)
        panel->battery_status = BATTERY_ABSENT;
    else if (panel->battery_percent < 0)
        panel->battery_status = BATTERY_UNAVAILABLE;
    else if (scePowerIsBatteryCharging() > 0)
        panel->battery_status = BATTERY_CHARGING;
    else if (scePowerIsPowerOnline() > 0)
        panel->battery_status = BATTERY_PLUGGED_IN;
    else
        panel->battery_status = BATTERY_DISCHARGING;
}

static void read_storage(Panel *panel)
{
    StickCapacity capacity = {0};
    StickCapacity *result = &capacity;
    // The firmware expects a pointer to the result buffer as its input argument.
    int status = sceIoDevctl("ms0:", MEMORY_STICK_CAPACITY, &result, sizeof(result), NULL, 0);
    uint64_t cluster = (uint64_t)capacity.sector_size * capacity.sectors_per_cluster;

    panel->total_bytes = 0;
    panel->free_bytes = 0;
    if (status >= 0 && cluster > 0 && cluster <= 1024 * 1024 &&
        capacity.total_clusters > 0 && capacity.free_clusters <= capacity.total_clusters) {
        panel->total_bytes = (uint64_t)capacity.total_clusters * cluster;
        panel->free_bytes = (uint64_t)capacity.free_clusters * cluster;
    }
}

static int impose_value(void *get_param, unsigned int parameter)
{
    struct KernelCallArg args = {.arg1 = parameter};
    if (!get_param || kuKernelCall(get_param, &args) < 0)
        return -1;
    return (int32_t)args.ret1;
}

static bool read_system_status(Panel *panel, void *get_param)
{
    int volume = impose_value(get_param, PSP_IMPOSE_MAIN_VOLUME);
    if (volume < 0 || volume > PANEL_VOLUME_MAX)
        volume = -1;
    int muted = volume >= 0 && impose_value(get_param, PSP_IMPOSE_MUTE) == 1;
    int wlan = sceWlanGetSwitchState();
    bool changed = panel->volume != volume || panel->volume_muted != muted ||
                   panel->wlan_switch != wlan;
    panel->volume = volume;
    panel->volume_muted = muted;
    panel->wlan_switch = wlan;
    return changed;
}

static int read_button_samples(void *peek_driver, SceCtrlData *samples, int count)
{
    // The driver reports the volume keys in the same samples as the other buttons.
    struct KernelCallArg args = {.arg1 = (uintptr_t)samples, .arg2 = count};
    if (peek_driver && kuKernelCall(peek_driver, &args) >= 0 &&
        args.ret1 > 0 && args.ret1 <= (unsigned int)count)
        return (int)args.ret1;
    return sceCtrlPeekBufferPositive(samples, count);
}

int main(void)
{
    SceUID thread = sceKernelCreateThread("PanelExitThread", exit_thread, 0x11, 0x1000, 0, NULL);
    if (thread >= 0)
        sceKernelStartThread(thread, 0, NULL);

    uint32_t *vram = sceGeEdramGetAddr();
    if (!vram || sceGeEdramGetSize() < 2 * BUFFER_BYTES ||
        sceDisplaySetMode(PSP_DISPLAY_MODE_LCD, PANEL_WIDTH, PANEL_HEIGHT) < 0) {
        sceKernelExitGame();
        return 1;
    }
    if (sceCtrlSetSamplingCycle(CONTROLLER_SAMPLE_US) < 0)
        sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    // ARK resolves the firmware's NIDs and calls these read-only driver functions.
    void *get_param = (void *)sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", 0x531C9778);
    void *peek_driver = (void *)sctrlHENFindFunction("sceController_Service", "sceCtrl_driver", 0x3A622550);
    Panel panel = {
        .language = load_language(system_language()),
        .stick_x = STICK_MIDPOINT,
        .stick_y = STICK_MIDPOINT,
        .volume = -1,
        .wlan_switch = -1,
    };
    ButtonTest button_test = {0};
    StickTest stick = {0};
    const unsigned int stick_shortcut = PSP_CTRL_SELECT | PSP_CTRL_CROSS;
    const unsigned int language_shortcut = PSP_CTRL_SELECT | PSP_CTRL_SQUARE;
    const unsigned int panel_shortcut_buttons = stick_shortcut | language_shortcut | PSP_CTRL_CIRCLE;
    unsigned int previous_buttons = 0;
    bool wait_panel_release = false;
    uint64_t next_battery = 0, next_storage = 0, next_system = 0;
    bool needs_draw = true;
    unsigned int back_buffer = 0;

    for (;;) {
        uint64_t now = sceKernelGetSystemTimeWide();
        if (stick.phase == STICK_CLOSED) {
            if (now >= next_battery) {
                read_battery(&panel);
                next_battery = now + BATTERY_REFRESH_US;
                needs_draw = true;
            }
            if (now >= next_storage) {
                read_storage(&panel);
                next_storage = now + STORAGE_REFRESH_US;
                needs_draw = true;
            }
            if (now >= next_system) {
                needs_draw |= read_system_status(&panel, get_param);
                next_system = now + SYSTEM_REFRESH_US;
            }
        }

        SceCtrlData pad;
        int have_pad = sceCtrlPeekBufferPositive(&pad, 1) > 0;
        if (have_pad && (pad.Buttons & PSP_CTRL_HOME)) {
            // Leave the system's exit dialog in control of the display.
            button_test_pause(&button_test, &panel);
            stick_test_pause(&stick);
            needs_draw = true;
            sceDisplayWaitVblankStart();
            continue;
        }

        if (have_pad) {
            unsigned int buttons = pad.Buttons;
            unsigned int newly_pressed = buttons & ~previous_buttons;
            if (stick.phase == STICK_CLOSED) {
                if ((buttons & stick_shortcut) == stick_shortcut && (newly_pressed & stick_shortcut)) {
                    button_test_pause(&button_test, &panel);
                    stick = (StickTest){.phase = STICK_READY};
                    needs_draw = true;
                } else if ((buttons & language_shortcut) == language_shortcut &&
                           (newly_pressed & language_shortcut)) {
                    button_test_pause(&button_test, &panel);
                    panel.language = panel.language == PANEL_SPANISH ? PANEL_ENGLISH : PANEL_SPANISH;
                    panel.language_unsaved = !save_language(panel.language);
                    wait_panel_release = true;
                    needs_draw = true;
                }
            } else if (newly_pressed & PSP_CTRL_CIRCLE) {
                stick.phase = STICK_CLOSED;
                wait_panel_release = true;
                needs_draw = true;
            } else if (newly_pressed & PSP_CTRL_CROSS) {
                needs_draw |= stick_test_confirm(&stick);
            }
            if (!(buttons & panel_shortcut_buttons))
                wait_panel_release = false;
            previous_buttons = buttons;

            if (panel.stick_x != pad.Lx || panel.stick_y != pad.Ly) {
                panel.stick_x = pad.Lx;
                panel.stick_y = pad.Ly;
                needs_draw = true;
            }
        }

        bool testing_stick = stick.phase != STICK_CLOSED;
        if (testing_stick) {
            // Both the cursor and the measurement use the game's analog input.
            if (have_pad)
                needs_draw |= stick_test_sample(&stick, pad.TimeStamp, pad.Lx, pad.Ly);
            else
                needs_draw |= stick_test_pause(&stick);
        } else if (!wait_panel_release) {
            SceCtrlData samples[CONTROLLER_HISTORY];
            int requested = button_test.have_sample ? CONTROLLER_HISTORY : 1;
            int count = read_button_samples(peek_driver, samples, requested);
            if (count > 0 && count <= requested) {
                // The button test also checks for releases between screen updates.
                for (int i = 0; i < count; ++i)
                    needs_draw |= button_test_sample(&button_test, &panel,
                                                     samples[i].TimeStamp, samples[i].Buttons);
            } else {
                needs_draw |= button_test_pause(&button_test, &panel);
            }
        }

        if (needs_draw) {
            uint32_t *frame = vram + back_buffer * BUFFER_PIXELS;
            if (testing_stick)
                stick_test_draw(frame, &panel, &stick);
            else
                panel_draw(frame, &panel);
            sceKernelDcacheWritebackRange(frame, BUFFER_BYTES);
            if (sceDisplaySetFrameBuf(frame, PANEL_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888,
                                      PSP_DISPLAY_SETBUF_NEXTFRAME) < 0)
                break;
            back_buffer ^= 1;
            needs_draw = false;
        }
        sceDisplayWaitVblankStart();
    }

    sceKernelExitGame();
    return 0;
}
