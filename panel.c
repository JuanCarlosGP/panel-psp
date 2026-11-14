#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "panel.h"
#include "button_test.h"

#define RGB(hex) (0xff000000u | (((hex) & 0xffu) << 16) | ((hex) & 0xff00u) | (((hex) >> 16) & 0xffu))

static const uint32_t text = RGB(0xeaf2fc);
static const uint32_t muted = RGB(0xa0b5cc);
static const uint32_t card = RGB(0x172335);
static const uint32_t border = RGB(0x2a3a50);
static const uint32_t well = RGB(0x101a28);
static const uint32_t blue = RGB(0x91c4ff);
static const uint32_t mint = RGB(0x74e8bb);
static const uint32_t violet = RGB(0xc5b0ff);
static const uint32_t hold_green = RGB(0x9ce48c);
static const uint32_t amber = RGB(0xffc580);

typedef struct {
    int8_t left, top;
    uint8_t width, height, advance;
    uint32_t offset;
} Glyph;

typedef struct {
    const Glyph *glyphs;
    const unsigned char *pixels;
} Font;

#include "font.inc"

static const char *localized(enum PanelLanguage language, const char *spanish, const char *english)
{
    return language == PANEL_ENGLISH ? english : spanish;
}

static char decimal_point(enum PanelLanguage language)
{
    return language == PANEL_ENGLISH ? '.' : ',';
}

static void box(uint32_t *pixels, int x, int y, int w, int h, uint32_t color)
{
    int right = x + w;
    int bottom = y + h;
    if (right > PANEL_WIDTH)
        right = PANEL_WIDTH;
    if (bottom > PANEL_HEIGHT)
        bottom = PANEL_HEIGHT;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= right || y >= bottom)
        return;

    for (int row = y; row < bottom; ++row) {
        uint32_t *start = pixels + row * PANEL_STRIDE;
        for (int col = x; col < right; ++col)
            start[col] = color;
    }
}

static void round_box(uint32_t *pixels, int x, int y, int w, int h, int radius, uint32_t color)
{
    box(pixels, x, y + radius, w, h - 2 * radius, color);
    for (int row = 0; row < radius; ++row) {
        int dy = radius - 1 - row;
        int span = radius - 1;
        while (span * span + dy * dy >= radius * radius)
            --span;
        int inset = radius - 1 - span;
        box(pixels, x + inset, y + row, w - 2 * inset, 1, color);
        box(pixels, x + inset, y + h - 1 - row, w - 2 * inset, 1, color);
    }
}

static void card_box(uint32_t *pixels, int x, int y, int w, int h)
{
    round_box(pixels, x, y, w, h, 8, border);
    round_box(pixels, x + 1, y + 1, w - 2, h - 2, 7, card);
}

static void stroke(uint32_t *pixels, int x, int y, int x_end, int y_end, uint32_t color)
{
    int dx = abs(x_end - x), dy = -abs(y_end - y);
    int step_x = x < x_end ? 1 : -1;
    int step_y = y < y_end ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        box(pixels, x, y, 2, 2, color);
        if (x == x_end && y == y_end)
            break;
        int twice = 2 * error;
        if (twice >= dy) {
            error += dy;
            x += step_x;
        }
        if (twice <= dx) {
            error += dx;
            y += step_y;
        }
    }
}

static void disk(uint32_t *pixels, int x, int y, int radius, uint32_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        int span = radius;
        while (span * span + dy * dy > radius * radius)
            --span;
        box(pixels, x - span, y + dy, 2 * span + 1, 1, color);
    }
}

static const Glyph *glyph_for(const Font *font, const unsigned char **str)
{
    unsigned int ch = *(*str)++;
    if (ch >= 0xc2 && ch <= 0xc3 && (**str & 0xc0) == 0x80)
        ch = ((ch & 0x1f) << 6) | (*(*str)++ & 0x3f);
    const Glyph *glyph = &font->glyphs[ch];
    return glyph->advance ? glyph : &font->glyphs['?'];
}

static int text_width(const Font *font, const char *str)
{
    const unsigned char *next = (const unsigned char *)str;
    int width = 0;
    while (*next)
        width += glyph_for(font, &next)->advance;
    return width;
}

static uint32_t blend(uint32_t bg, uint32_t fg, unsigned int opacity)
{
    const uint32_t mask = 0x00ff00ff;
    uint32_t rb = ((bg & mask) * (256 - opacity) + (fg & mask) * opacity) >> 8;
    uint32_t g = ((bg & 0xff00) * (256 - opacity) + (fg & 0xff00) * opacity) >> 8;
    return 0xff000000 | (rb & mask) | (g & 0xff00);
}

static void draw_text(uint32_t *pixels, const Font *font, int x, int baseline,
                      const char *str, uint32_t color)
{
    const unsigned char *next = (const unsigned char *)str;
    while (*next) {
        const Glyph *glyph = glyph_for(font, &next);
        const unsigned char *bitmap = font->pixels + glyph->offset;
        int left = x + glyph->left;
        int top = baseline + glyph->top;

        for (int y = 0; y < glyph->height; ++y) {
            if ((unsigned int)(top + y) >= PANEL_HEIGHT)
                continue;
            for (int col = 0; col < glyph->width; ++col) {
                if ((unsigned int)(left + col) >= PANEL_WIDTH)
                    continue;
                unsigned int a = bitmap[y * glyph->width + col];
                uint32_t *pixel = pixels + (top + y) * PANEL_STRIDE + left + col;
                if (a)
                    *pixel = a == 255 ? color : blend(*pixel, color, a);
            }
        }
        x += glyph->advance;
    }
}

static void draw_meter(uint32_t *pixels, int x, int y, int width, unsigned int percent,
                       uint32_t color)
{
    round_box(pixels, x, y, width, 6, 3, well);
    int fill = width * percent / 100;
    if (fill >= 6)
        round_box(pixels, x, y, fill, 6, 3, color);
    else if (fill > 0)
        box(pixels, x + 1, y + 1, fill, 4, color);
}

static void draw_hold_progress(uint32_t *pixels, const Panel *panel)
{
    char duration[16];
    unsigned int tenths = panel->hold_percent * BUTTON_HOLD_SECONDS / 10;
    if (panel->pressed && panel->hold_percent < 100)
        snprintf(duration, sizeof duration, "%u%c%u s", tenths / 10, decimal_point(panel->language), tenths % 10);
    else
        snprintf(duration, sizeof duration, "%d s", BUTTON_HOLD_SECONDS);

    uint32_t color = panel->hold_percent == 100 ? hold_green : blue;
    round_box(pixels, 154, 16, 166, 24, 8, border);
    round_box(pixels, 155, 17, 164, 22, 7, card);
    draw_text(pixels, &label_font, 166, 32, localized(panel->language, "Mantén", "Hold"), text);
    draw_meter(pixels, 216, 25, 60, panel->hold_percent, color);
    draw_text(pixels, &small_font, 309 - text_width(&small_font, duration), 32, duration,
              panel->pressed ? color : muted);
}

static void format_size(char *out, size_t size, uint64_t bytes, char point)
{
    uint64_t unit = UINT64_C(1024) * 1024;
    const char *suffix = "MiB";
    if (bytes >= unit * 1024) {
        unit *= 1024;
        suffix = "GiB";
    }
    if (bytes >= unit * 1024) {
        unit *= 1024;
        suffix = "TiB";
    }
    if (point) {
        unsigned long whole = (unsigned long)(bytes / unit);
        unsigned int fraction = (unsigned int)((bytes % unit) * 10 / unit);
        snprintf(out, size, "%lu%c%u %s", whole, point, fraction, suffix);
    } else {
        snprintf(out, size, "%lu %s", (unsigned long)((bytes + unit / 2) / unit), suffix);
    }
}

static const char *battery_status_text(enum PanelLanguage language, enum BatteryStatus status)
{
    switch (status) {
    case BATTERY_ABSENT: return localized(language, "Sin batería", "No battery");
    case BATTERY_CHARGING: return localized(language, "Cargando", "Charging");
    case BATTERY_PLUGGED_IN: return localized(language, "Conectada", "Plugged in");
    case BATTERY_DISCHARGING: return localized(language, "En batería", "On battery");
    default: return localized(language, "Sin lectura", "Unavailable");
    }
}

static void draw_status(uint32_t *pixels, const Panel *panel)
{
    enum PanelLanguage language = panel->language;
    const char *unavailable = localized(language, "Sin lectura", "Unavailable");
    char battery[16] = "--";
    char space[32] = "--";
    char total[36];
    const char *total_label = unavailable;
    char volume[16] = "--";
    unsigned int battery_fill = 0;
    unsigned int free_fill = 0;
    unsigned int volume_fill = 0;
    uint32_t battery_color = mint;

    if (panel->battery_percent >= 0) {
        snprintf(battery, sizeof battery, "%d %%", panel->battery_percent);
        battery_fill = (unsigned int)panel->battery_percent;
        if (battery_fill < 20)
            battery_color = amber;
    }
    if (panel->total_bytes) {
        format_size(space, sizeof space, panel->free_bytes, decimal_point(language));
        char capacity[32];
        format_size(capacity, sizeof capacity, panel->total_bytes, 0);
        snprintf(total, sizeof total, localized(language, "de %s", "of %s"), capacity);
        total_label = total;
        free_fill = (unsigned int)(panel->free_bytes * 100 / panel->total_bytes);
    }
    if (panel->volume >= 0) {
        snprintf(volume, sizeof volume, "%d", panel->volume);
        if (!panel->volume_muted)
            volume_fill = (unsigned int)panel->volume * 100 / PANEL_VOLUME_MAX;
    }

    card_box(pixels, 16, 49, 128, 77);
    card_box(pixels, 154, 49, 166, 77);
    card_box(pixels, 330, 49, 134, 77);
    draw_text(pixels, &label_font, 28, 67, localized(language, "Batería", "Battery"), muted);
    draw_text(pixels, &label_font, 166, 67, localized(language, "Espacio libre", "Free space"), muted);
    draw_text(pixels, &label_font, 342, 67, localized(language, "Volumen", "Volume"), muted);
    draw_text(pixels, &small_font, 28, 107, battery_status_text(language, panel->battery_status), muted);
    draw_text(pixels, &small_font, 166, 107, total_label, muted);

    draw_text(pixels, &large_font, 28, 93, battery, text);
    draw_text(pixels, &large_font, 166, 93, space, text);
    draw_text(pixels, &large_font, 342, 93, volume, text);
    if (panel->volume >= 0) {
        draw_text(pixels, &small_font, 346 + text_width(&large_font, volume), 92, "/ 30", muted);
        if (panel->volume_muted)
            draw_text(pixels, &small_font, 342, 107, localized(language, "Silencio", "Muted"), violet);
    } else {
        draw_text(pixels, &small_font, 342, 107, unavailable, muted);
    }
    draw_meter(pixels, 28, 113, 104, battery_fill, battery_color);
    draw_meter(pixels, 166, 113, 142, free_fill, blue);
    draw_meter(pixels, 342, 113, 110, volume_fill, violet);
}

enum Symbol { UP, DOWN, LEFT, RIGHT, TRIANGLE, CIRCLE, CROSS, SQUARE, LABEL };

typedef struct {
    unsigned int mask;
    int x, y, width;
    enum Symbol symbol;
    const char *label;
} Button;

static const Button buttons[] = {
    {PSP_CTRL_UP,       58, 174, 28, UP, NULL},
    {PSP_CTRL_LEFT,     28, 194, 28, LEFT, NULL},
    {PSP_CTRL_RIGHT,    88, 194, 28, RIGHT, NULL},
    {PSP_CTRL_DOWN,     58, 214, 28, DOWN, NULL},
    {PSP_CTRL_TRIANGLE, 168, 174, 28, TRIANGLE, NULL},
    {PSP_CTRL_SQUARE,   138, 194, 28, SQUARE, NULL},
    {PSP_CTRL_CIRCLE,   198, 194, 28, CIRCLE, NULL},
    {PSP_CTRL_CROSS,    168, 214, 28, CROSS, NULL},
    {PSP_CTRL_LTRIGGER, 246, 176, 45, LABEL, "L"},
    {PSP_CTRL_RTRIGGER, 296, 176, 45, LABEL, "R"},
    {PSP_CTRL_SELECT,   246, 207, 45, LABEL, "SELECT"},
    {PSP_CTRL_START,    296, 207, 45, LABEL, "START"},
    {PSP_CTRL_VOLDOWN,  401,  56, 23, LABEL, "-"},
    {PSP_CTRL_VOLUP,    429,  56, 23, LABEL, "+"},
};

static void draw_symbol(uint32_t *pixels, enum Symbol symbol, int cx, int cy,
                        uint32_t ink, uint32_t fill)
{
    switch (symbol) {
    case UP:
        stroke(pixels, cx - 4, cy + 2, cx, cy - 2, ink);
        stroke(pixels, cx, cy - 2, cx + 4, cy + 2, ink);
        break;
    case DOWN:
        stroke(pixels, cx - 4, cy - 2, cx, cy + 2, ink);
        stroke(pixels, cx, cy + 2, cx + 4, cy - 2, ink);
        break;
    case LEFT:
        stroke(pixels, cx + 2, cy - 4, cx - 2, cy, ink);
        stroke(pixels, cx - 2, cy, cx + 2, cy + 4, ink);
        break;
    case RIGHT:
        stroke(pixels, cx - 2, cy - 4, cx + 2, cy, ink);
        stroke(pixels, cx + 2, cy, cx - 2, cy + 4, ink);
        break;
    case TRIANGLE:
        stroke(pixels, cx - 4, cy + 3, cx, cy - 4, ink);
        stroke(pixels, cx, cy - 4, cx + 4, cy + 3, ink);
        stroke(pixels, cx - 4, cy + 3, cx + 4, cy + 3, ink);
        break;
    case CIRCLE:
        disk(pixels, cx, cy, 5, ink);
        disk(pixels, cx, cy, 3, fill);
        break;
    case CROSS:
        stroke(pixels, cx - 4, cy - 4, cx + 3, cy + 3, ink);
        stroke(pixels, cx + 3, cy - 4, cx - 4, cy + 3, ink);
        break;
    case SQUARE:
        box(pixels, cx - 5, cy - 4, 10, 10, ink);
        box(pixels, cx - 3, cy - 2, 6, 6, fill);
        break;
    case LABEL:
        break;
    }
}

static void draw_button(uint32_t *pixels, const Button *button, const Panel *panel)
{
    const int x = button->x, y = button->y, w = button->width;
    const int cx = x + w / 2, cy = y + 8;
    uint32_t rim = RGB(0x3b4e67);
    uint32_t fill = well;
    uint32_t ink = muted;

    if (panel->pressed & button->mask) {
        rim = fill = (panel->holding_complete & button->mask) ? hold_green : mint;
        ink = well;
    } else if (panel->validated & button->mask) {
        rim = RGB(0x68ad85);
        fill = RGB(0x203e38);
        ink = RGB(0xb5e6c8);
    } else if (panel->tested & button->mask) {
        rim = RGB(0x5899cb);
        fill = RGB(0x213853);
        ink = RGB(0xc0dcff);
    }
    round_box(pixels, x, y, w, 18, 5, rim);
    round_box(pixels, x + 1, y + 1, w - 2, 16, 4, fill);

    if (button->symbol == LABEL) {
        const Font *font = button->mask & (PSP_CTRL_SELECT | PSP_CTRL_START) ? &button_font : &small_font;
        draw_text(pixels, font, cx - text_width(font, button->label) / 2,
                  y + 13, button->label, ink);
    } else {
        draw_symbol(pixels, button->symbol, cx, cy, ink, fill);
    }
}

static void draw_stick(uint32_t *pixels, unsigned int x, unsigned int y)
{
    const int left = 367, top = 176, size = 49;
    round_box(pixels, left, top, size, size, 6, border);
    round_box(pixels, left + 1, top + 1, size - 2, size - 2, 5, well);
    box(pixels, left + 5, top + size / 2, size - 10, 1, border);
    box(pixels, left + size / 2, top + 5, 1, size - 10, border);
    int dot_x = left + 5 + (int)x * (size - 11) / 255;
    int dot_y = top + 5 + (int)y * (size - 11) / 255;
    disk(pixels, dot_x, dot_y, 4, mint);

    char axis[12];
    snprintf(axis, sizeof axis, "X %u", x);
    draw_text(pixels, &small_font, 422, 194, axis, text);
    snprintf(axis, sizeof axis, "Y %u", y);
    draw_text(pixels, &small_font, 422, 214, axis, text);
}

static void draw_background(uint32_t *pixels)
{
    for (int y = 0; y < PANEL_HEIGHT; ++y) {
        unsigned int shade = (unsigned int)y * 9 / PANEL_HEIGHT;
        box(pixels, 0, y, PANEL_WIDTH, 1,
            0xff18100b + ((shade * 2) << 16) + (shade << 8) + shade);
    }
}

static const char shortcut_prefix[] = "SELECT +";
enum { FOOTER_BASELINE = 260, FOOTER_ICON_GAP = 12 };

static void draw_shortcut(uint32_t *pixels, int x, int action_offset, enum Symbol symbol)
{
    draw_text(pixels, &small_font, x, FOOTER_BASELINE, shortcut_prefix, blue);
    draw_symbol(pixels, symbol, x + action_offset - FOOTER_ICON_GAP, FOOTER_BASELINE - 5, blue, well);
}

static void draw_panel_footer(uint32_t *pixels, enum PanelLanguage language)
{
    const char *stick = localized(language, "Probar stick", "Test stick");
    const char *clear = localized(language, "Borrar", "Clear");
    const int action_offset = text_width(&small_font, shortcut_prefix) + 2 * FOOTER_ICON_GAP;
    const int clear_x = (PANEL_WIDTH - action_offset - text_width(&small_font, clear)) / 2;
    const int language_x = 464 - action_offset - 50;
    const int switch_x = language_x + action_offset;
    bool english = language == PANEL_ENGLISH;

    draw_shortcut(pixels, 16, action_offset, CROSS);
    draw_text(pixels, &small_font, 16 + action_offset, FOOTER_BASELINE, stick, muted);

    draw_shortcut(pixels, clear_x, action_offset, CIRCLE);
    draw_text(pixels, &small_font, clear_x + action_offset, FOOTER_BASELINE, clear, muted);

    draw_shortcut(pixels, language_x, action_offset, SQUARE);
    round_box(pixels, switch_x + (english ? 27 : 0), FOOTER_BASELINE - 12, 23, 16, 4, RGB(0x213853));
    box(pixels, switch_x + 24, FOOTER_BASELINE - 10, 1, 11, muted);
    draw_text(pixels, &small_font, switch_x + 3, FOOTER_BASELINE, "ES", english ? muted : blue);
    draw_text(pixels, &small_font, switch_x + 30, FOOTER_BASELINE, "EN", english ? blue : muted);
}

void panel_draw(uint32_t *pixels, const Panel *panel)
{
    enum PanelLanguage language = panel->language;
    draw_background(pixels);
    draw_text(pixels, &large_font, 16, 35, "Panel PSP", text);
    draw_hold_progress(pixels, panel);
    const char *wlan = panel->wlan_switch < 0 ? localized(language, "Sin lectura", "Unknown") :
                       panel->wlan_switch > 0 ? localized(language, "Activado", "On") :
                       localized(language, "Apagado", "Off");
    uint32_t wlan_color = panel->wlan_switch > 0 ? mint : muted;
    round_box(pixels, 331, 16, 133, 24, 8, border);
    round_box(pixels, 332, 17, 131, 22, 7, card);
    disk(pixels, 343, 28, 3, wlan_color);
    draw_text(pixels, &label_font, 353, 32, "WLAN", text);
    draw_text(pixels, &small_font, 453 - text_width(&small_font, wlan), 32, wlan, wlan_color);
    draw_status(pixels, panel);

    card_box(pixels, 16, 132, 448, 109);
    draw_text(pixels, &label_font, 28, 149, localized(language, "Prueba de botones", "Button test"), text);
    unsigned int count = 0;
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; ++i)
        count += (panel->tested & buttons[i].mask) != 0;
    char tested[24];
    snprintf(tested, sizeof tested, localized(language, "%u/%u probados", "%u/%u tested"), count,
             (unsigned int)(sizeof buttons / sizeof buttons[0]));
    const char *status = panel->language_unsaved ?
                         localized(language, "Idioma sin guardar", "Language not saved") : tested;
    uint32_t status_color = panel->language_unsaved ? amber :
                            count == sizeof buttons / sizeof buttons[0] ? mint : muted;
    draw_text(pixels, &small_font, 452 - text_width(&small_font, status), 149, status, status_color);

    draw_text(pixels, &small_font, 28, 166, localized(language, "Cruceta", "D-pad"), muted);
    draw_text(pixels, &small_font, 138, 166, localized(language, "Símbolos", "Face buttons"), muted);
    draw_text(pixels, &small_font, 246, 166, localized(language, "Gatillos y menú", "Triggers & menu"), muted);
    draw_text(pixels, &small_font, 367, 166, localized(language, "Stick analógico", "Analog stick"), muted);
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; ++i)
        draw_button(pixels, &buttons[i], panel);
    draw_stick(pixels, panel->stick_x, panel->stick_y);

    draw_panel_footer(pixels, language);
}

static void draw_stick_map(uint32_t *pixels, const Panel *panel, const StickTest *test)
{
    enum { HALF_WIDTH = 64 };
    const int cx = 104, cy = 143;
    int dx = (int)panel->stick_x - STICK_MIDPOINT;
    int dy = (int)panel->stick_y - STICK_MIDPOINT;
    unsigned int distance_squared = dx * dx + dy * dy;
    bool measuring_center = test->phase == STICK_REST || test->phase == STICK_RETURN_OBSERVE;
    bool zoom = test->phase == STICK_REST || (test->phase == STICK_RETURN_OBSERVE && test->after_release.samples);
    const int extent = zoom ? 32 : STICK_MIDPOINT;
    const int dead_radius = HALF_WIDTH * STICK_CENTER_TOLERANCE / extent;

    box(pixels, cx - HALF_WIDTH, cy - HALF_WIDTH, HALF_WIDTH * 2 + 1, HALF_WIDTH * 2 + 1, border);
    box(pixels, cx - HALF_WIDTH + 1, cy - HALF_WIDTH + 1, HALF_WIDTH * 2 - 1, HALF_WIDTH * 2 - 1, well);
    box(pixels, cx - HALF_WIDTH + 1, cy, HALF_WIDTH * 2 - 1, 1, border);
    box(pixels, cx, cy - HALF_WIDTH + 1, 1, HALF_WIDTH * 2 - 1, border);
    disk(pixels, cx, cy, dead_radius + 1, RGB(0x55708c));
    disk(pixels, cx, cy, dead_radius, RGB(0x27405b));

    if (test->phase >= STICK_RANGE_READY) {
        const int x_offset[STICK_DIRECTIONS] = {0, HALF_WIDTH, HALF_WIDTH, HALF_WIDTH, 0, -HALF_WIDTH, -HALF_WIDTH, -HALF_WIDTH};
        const int y_offset[STICK_DIRECTIONS] = {-HALF_WIDTH, -HALF_WIDTH, 0, HALF_WIDTH, HALF_WIDTH, HALF_WIDTH, 0, -HALF_WIDTH};
        for (int i = 0; i < STICK_DIRECTIONS; ++i) {
            uint32_t color = test->reached & (1u << i) ? hold_green : RGB(0x55708c);
            disk(pixels, cx + x_offset[i], cy + y_offset[i], 4, color);
        }
    }

    int draw_x = dx < -extent ? -extent : dx > extent ? extent : dx;
    int draw_y = dy < -extent ? -extent : dy > extent ? extent : dy;
    uint32_t color = measuring_center && distance_squared > STICK_CENTER_TOLERANCE * STICK_CENTER_TOLERANCE ? amber : mint;
    disk(pixels, cx + draw_x * HALF_WIDTH / extent, cy + draw_y * HALF_WIDTH / extent, 3, color);

    char axes[28];
    snprintf(axes, sizeof axes, "X %d   Y %d", dx, dy);
    draw_text(pixels, &small_font, cx - text_width(&small_font, axes) / 2, 226, axes, text);
    if (zoom) {
        const char *scale = localized(panel->language, "Centro ampliado", "Center zoom");
        draw_text(pixels, &small_font, cx - text_width(&small_font, scale) / 2, 66, scale, muted);
    }
}

static unsigned int reached_directions(const StickTest *test)
{
    unsigned int count = 0;
    for (int i = 0; i < STICK_DIRECTIONS; ++i)
        count += (test->reached & (1u << i)) != 0;
    return count;
}

static void draw_center_result(uint32_t *pixels, enum PanelLanguage language, int y,
                               const char *label, const StickCenter *center, const char *override)
{
    if (!center->samples)
        return;
    unsigned int span_x = center->max_x - center->min_x;
    unsigned int span_y = center->max_y - center->min_y;
    bool jitter = span_x > STICK_JITTER_TOLERANCE || span_y > STICK_JITTER_TOLERANCE;
    const char *result = localized(language, "Estable", "Stable");
    uint32_t color = hold_green;
    if (override) {
        result = override;
        color = amber;
    } else if (center->drift) {
        result = localized(language, "Desvío sostenido", "Sustained drift");
        color = amber;
    } else if (jitter || center->peak_distance_squared > STICK_CENTER_TOLERANCE * STICK_CENTER_TOLERANCE) {
        result = localized(language, "Oscilación", "Jitter");
        color = amber;
    }

    int average_x = (int)((center->sum_x + center->samples / 2) / center->samples) - STICK_MIDPOINT;
    int average_y = (int)((center->sum_y + center->samples / 2) / center->samples) - STICK_MIDPOINT;
    unsigned int peak = (unsigned int)(sqrtf((float)center->peak_distance_squared) + 0.5f);
    char readings[80];
    snprintf(readings, sizeof readings,
             localized(language, "X %+d / Y %+d de media  ·  Pico %u", "Avg X %+d / Y %+d  ·  Peak %u"),
             average_x, average_y, peak);
    draw_text(pixels, &label_font, 216, y, label, text);
    draw_text(pixels, &small_font, 452 - text_width(&small_font, result), y, result, color);
    draw_text(pixels, &small_font, 216, y + 16, readings, muted);
    snprintf(readings, sizeof readings,
             localized(language, "Variación X %u / Y %u puntos", "Variation X %u / Y %u points"), span_x, span_y);
    draw_text(pixels, &small_font, 216, y + 32, readings, muted);
}

static const char *direction_name(enum PanelLanguage language, enum StickDirection direction)
{
    static const char *const spanish[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
    static const char *const english[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return localized(language, spanish[direction], english[direction]);
}

static void draw_return_status(uint32_t *pixels, enum PanelLanguage language, const StickTest *test, int y)
{
    const char *warning = NULL;
    if (!test->reached_center)
        warning = localized(language, "No llegó al centro", "Didn't reach center");
    else if (!test->return_peak)
        warning = localized(language, "Tope sin confirmar", "Stop not reached");
    draw_center_result(pixels, language, y, localized(language, "Vuelta al centro", "Return to center"),
                       &test->after_release, warning);
}

static void draw_direction_scores(uint32_t *pixels, enum PanelLanguage language, const StickTest *test,
                                  int top, int height)
{
    for (int i = 0; i < STICK_DIRECTIONS; ++i) {
        int x = 216 + (i % 4) * 60;
        int y = top + (i / 4) * (height + 4);
        bool reached = (test->reached & (1u << i)) != 0;
        round_box(pixels, x, y, 56, height, 5, reached ? RGB(0x203e38) : well);
        int baseline = y + (height + 9) / 2;
        draw_text(pixels, &small_font, x + 3, baseline, direction_name(language, i), reached ? hold_green : muted);
        char score[12] = "--";
        if (test->range_peak[i])
            snprintf(score, sizeof score, "%u%%", (unsigned int)test->range_peak[i]);
        draw_text(pixels, &button_font, x + 53 - text_width(&button_font, score), baseline, score,
                  reached ? hold_green : text);
    }
}

static void draw_stick_timer(uint32_t *pixels, enum PanelLanguage language, uint32_t elapsed,
                             unsigned int seconds, int y)
{
    char timer[24];
    unsigned int remaining = seconds * 10 - elapsed / 100000;
    snprintf(timer, sizeof timer, localized(language, "Midiendo  %u%c%u s", "Measuring  %u%c%u s"),
             remaining / 10, decimal_point(language), remaining % 10);
    draw_text(pixels, &label_font, 216, y, timer, mint);
    draw_meter(pixels, 216, y + 10, 236, elapsed / (seconds * 10000), mint);
}

void stick_test_draw(uint32_t *pixels, const Panel *panel, const StickTest *test)
{
    enum PanelLanguage language = panel->language;
    const char *rest_label = localized(language, "Reposo", "At rest");
    const char *range_title = localized(language, "3/3  Recorrido por el borde", "3/3  Outer range");
    const char *range_instruction = localized(language, "Da una vuelta lenta por todo el borde.",
                                              "Move slowly around the outer edge.");
    draw_background(pixels);
    draw_text(pixels, &large_font, 16, 35, localized(language, "Stick analógico", "Analog stick"), text);
    card_box(pixels, 16, 49, 177, 195);
    card_box(pixels, 204, 49, 260, 195);
    draw_stick_map(pixels, panel, test);

    const char *confirm = NULL;
    if (test->phase <= STICK_REST) {
        draw_text(pixels, &label_font, 216, 72, localized(language, "1/3  Reposo", "1/3  At rest"), blue);
        draw_text(pixels, &small_font, 216, 96,
                  localized(language, "Deja el stick sin tocar.", "Leave the stick untouched."), text);
        if (test->phase == STICK_READY) {
            confirm = localized(language, "Empezar", "Start");
        } else if (test->phase == STICK_SETTLE) {
            draw_text(pixels, &label_font, 216, 147,
                      localized(language, "Preparando...", "Get ready..."), mint);
        } else {
            draw_stick_timer(pixels, language, test->elapsed_us, STICK_REST_SECONDS, 147);
            draw_center_result(pixels, language, 189, rest_label, &test->rest, NULL);
        }
    } else if (test->phase < STICK_RANGE_READY) {
        draw_text(pixels, &label_font, 216, 72,
                  localized(language, "2/3  Vuelta al centro", "2/3  Return to center"), blue);
        const char *released = localized(language, "Ya lo solté", "I've let go");
        if (test->phase == STICK_RETURN_TARGET) {
            draw_text(pixels, &small_font, 216, 96,
                      localized(language, "Empuja hasta el tope en un eje X o Y.",
                                "Push the stick all the way on one axis."), text);
            draw_text(pixels, &small_font, 216, 113,
                      localized(language, "Elige arriba, abajo, izquierda o derecha.",
                                "Choose up, down, left, or right."), muted);
            confirm = released;
        } else if (test->phase == STICK_RETURN_RELEASE) {
            char limit[48];
            snprintf(limit, sizeof limit, localized(language, "Tope %s detectado. Suelta el stick.",
                                                    "%s stop reached. Let go."),
                     direction_name(language, test->return_direction));
            draw_text(pixels, &label_font, 216, 97, limit, mint);
            draw_text(pixels, &small_font, 216, 117,
                      localized(language, "Si se queda atascado, pulsa X al soltar.",
                                "If it sticks, press X after letting go."), muted);
            confirm = released;
        } else {
            draw_text(pixels, &small_font, 216, 96,
                      localized(language, "Deja que el stick vuelva solo al centro.",
                                "Let the stick return to center on its own."), text);
            draw_stick_timer(pixels, language, test->elapsed_us, STICK_RETURN_SECONDS, 133);
        }
        draw_center_result(pixels, language, 184, rest_label, &test->rest, NULL);
    } else if (test->phase == STICK_RANGE_READY) {
        draw_text(pixels, &label_font, 216, 72, range_title, blue);
        draw_text(pixels, &small_font, 216, 97, range_instruction, text);
        draw_return_status(pixels, language, test, 140);
        confirm = localized(language, "Empezar recorrido", "Start sweep");
    } else {
        unsigned int count = reached_directions(test);
        bool results = test->phase == STICK_RESULTS;
        const char *title = results ? localized(language, "Resultado del stick", "Stick test results") : range_title;
        draw_text(pixels, &label_font, 216, 72, title, blue);
        char reached[45];
        snprintf(reached, sizeof reached,
                 localized(language, "%u/%u direcciones  ·  Objetivo %u%%", "%u/%u directions  ·  Target %u%%"), count,
                 (unsigned int)STICK_DIRECTIONS, (unsigned int)STICK_RANGE_TARGET);
        uint32_t range_color = count == STICK_DIRECTIONS ? hold_green : muted;
        if (results) {
            draw_center_result(pixels, language, 92, rest_label, &test->rest, NULL);
            draw_return_status(pixels, language, test, 143);
            draw_text(pixels, &small_font, 216, 193, reached, range_color);
            draw_direction_scores(pixels, language, test, 198, 18);
        } else {
            draw_text(pixels, &small_font, 216, 97, range_instruction, text);
            draw_text(pixels, &small_font, 216, 122, reached, range_color);
            draw_direction_scores(pixels, language, test, 133, 23);
        }
        confirm = results ? localized(language, "Repetir", "Run again") :
                            localized(language, "Ver resultado", "View results");
    }
    if (confirm) {
        draw_text(pixels, &label_font, 16, 260, "X", blue);
        draw_text(pixels, &small_font, 30, 260, confirm, muted);
    }
    const char *circle = localized(language, "Círculo", "Circle");
    const char *back = localized(language, "Volver al panel", "Back to panel");
    int back_left = 452 - text_width(&small_font, back);
    draw_text(pixels, &label_font, back_left - 10 - text_width(&label_font, circle), 260, circle, blue);
    draw_text(pixels, &small_font, back_left, 260, back, muted);
}
