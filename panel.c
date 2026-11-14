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
        snprintf(duration, sizeof duration, "%u,%u s", tenths / 10, tenths % 10);
    else
        snprintf(duration, sizeof duration, "%d s", BUTTON_HOLD_SECONDS);

    uint32_t color = panel->hold_percent == 100 ? hold_green : blue;
    round_box(pixels, 154, 16, 166, 24, 8, border);
    round_box(pixels, 155, 17, 164, 22, 7, card);
    draw_text(pixels, &label_font, 166, 32, "Mantén", text);
    draw_meter(pixels, 216, 25, 60, panel->hold_percent, color);
    draw_text(pixels, &small_font, 309 - text_width(&small_font, duration), 32, duration,
              panel->pressed ? color : muted);
}

static void format_size(char *out, size_t size, uint64_t bytes, int decimal)
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
    if (decimal) {
        unsigned long whole = (unsigned long)(bytes / unit);
        unsigned int fraction = (unsigned int)((bytes % unit) * 10 / unit);
        snprintf(out, size, "%lu,%u %s", whole, fraction, suffix);
    } else {
        snprintf(out, size, "%lu %s", (unsigned long)((bytes + unit / 2) / unit), suffix);
    }
}

static void draw_status(uint32_t *pixels, const Panel *panel)
{
    char battery[16] = "--";
    char space[32] = "--";
    char total[36] = "Sin lectura";
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
        format_size(space, sizeof space, panel->free_bytes, 1);
        char capacity[32];
        format_size(capacity, sizeof capacity, panel->total_bytes, 0);
        snprintf(total, sizeof total, "de %s", capacity);
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
    draw_text(pixels, &label_font, 28, 67, "Batería", muted);
    draw_text(pixels, &label_font, 166, 67, "Espacio libre", muted);
    draw_text(pixels, &label_font, 342, 67, "Volumen", muted);
    draw_text(pixels, &small_font, 28, 107, panel->battery_status, muted);
    draw_text(pixels, &small_font, 166, 107, total, muted);

    draw_text(pixels, &large_font, 28, 93, battery, text);
    draw_text(pixels, &large_font, 166, 93, space, text);
    draw_text(pixels, &large_font, 342, 93, volume, text);
    if (panel->volume >= 0) {
        draw_text(pixels, &small_font, 346 + text_width(&large_font, volume), 92, "/ 30", muted);
        if (panel->volume_muted)
            draw_text(pixels, &small_font, 342, 107, "Silencio", violet);
    } else {
        draw_text(pixels, &small_font, 342, 107, "Sin lectura", muted);
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

    switch (button->symbol) {
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
    case LABEL: {
        const Font *font = button->mask & (PSP_CTRL_SELECT | PSP_CTRL_START) ? &button_font : &small_font;
        draw_text(pixels, font, cx - text_width(font, button->label) / 2,
                  y + 13, button->label, ink);
        break;
    }
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

void panel_draw(uint32_t *pixels, const Panel *panel)
{
    draw_background(pixels);
    draw_text(pixels, &large_font, 16, 35, "Panel PSP", text);
    draw_hold_progress(pixels, panel);
    const char *wlan = panel->wlan_switch < 0 ? "Sin lectura" :
                       panel->wlan_switch > 0 ? "Activado" : "Apagado";
    uint32_t wlan_color = panel->wlan_switch > 0 ? mint : muted;
    round_box(pixels, 331, 16, 133, 24, 8, border);
    round_box(pixels, 332, 17, 131, 22, 7, card);
    disk(pixels, 343, 28, 3, wlan_color);
    draw_text(pixels, &label_font, 353, 32, "WLAN", text);
    draw_text(pixels, &small_font, 453 - text_width(&small_font, wlan), 32, wlan, wlan_color);
    draw_status(pixels, panel);

    card_box(pixels, 16, 132, 448, 109);
    draw_text(pixels, &label_font, 28, 149, "Prueba de botones", text);
    unsigned int count = 0;
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; ++i)
        count += (panel->tested & buttons[i].mask) != 0;
    char tested[24];
    snprintf(tested, sizeof tested, "%u/%u probados", count,
             (unsigned int)(sizeof buttons / sizeof buttons[0]));
    draw_text(pixels, &small_font, 452 - text_width(&small_font, tested), 149, tested,
              count == sizeof buttons / sizeof buttons[0] ? mint : muted);

    draw_text(pixels, &small_font, 28, 166, "Cruceta", muted);
    draw_text(pixels, &small_font, 138, 166, "Símbolos", muted);
    draw_text(pixels, &small_font, 246, 166, "Gatillos y menú", muted);
    draw_text(pixels, &small_font, 367, 166, "Stick analógico", muted);
    for (size_t i = 0; i < sizeof buttons / sizeof buttons[0]; ++i)
        draw_button(pixels, &buttons[i], panel);
    draw_stick(pixels, panel->stick_x, panel->stick_y);

    const char *exit = "Salir";
    const char *stick = "SELECT + X   Probar stick";
    const char *reset = "Mantén SELECT + START   Borrar marcas";
    int exit_right = 38 + text_width(&small_font, exit);
    int reset_left = 464 - text_width(&small_font, reset);
    int stick_left = (exit_right + reset_left - text_width(&small_font, stick)) / 2;
    draw_text(pixels, &label_font, 16, 260, "PS", blue);
    draw_text(pixels, &small_font, 38, 260, exit, muted);
    draw_text(pixels, &small_font, stick_left, 260, stick, blue);
    draw_text(pixels, &small_font, reset_left, 260, reset, muted);
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
        const char *scale = "Centro ampliado";
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

static void draw_center_result(uint32_t *pixels, int y, const char *label,
                               const StickCenter *center, const char *override)
{
    if (!center->samples)
        return;
    unsigned int span_x = center->max_x - center->min_x;
    unsigned int span_y = center->max_y - center->min_y;
    bool jitter = span_x > STICK_JITTER_TOLERANCE || span_y > STICK_JITTER_TOLERANCE;
    const char *result = "Estable";
    uint32_t color = hold_green;
    if (override) {
        result = override;
        color = amber;
    } else if (center->drift) {
        result = "Desvío sostenido";
        color = amber;
    } else if (jitter || center->peak_distance_squared > STICK_CENTER_TOLERANCE * STICK_CENTER_TOLERANCE) {
        result = "Oscilación";
        color = amber;
    }

    int average_x = (int)((center->sum_x + center->samples / 2) / center->samples) - STICK_MIDPOINT;
    int average_y = (int)((center->sum_y + center->samples / 2) / center->samples) - STICK_MIDPOINT;
    unsigned int peak = (unsigned int)(sqrtf((float)center->peak_distance_squared) + 0.5f);
    char readings[80];
    snprintf(readings, sizeof readings, "X %+d / Y %+d de media  ·  Pico %u", average_x, average_y, peak);
    draw_text(pixels, &label_font, 216, y, label, text);
    draw_text(pixels, &small_font, 452 - text_width(&small_font, result), y, result, color);
    draw_text(pixels, &small_font, 216, y + 16, readings, muted);
    snprintf(readings, sizeof readings, "Variación X %u / Y %u puntos", span_x, span_y);
    draw_text(pixels, &small_font, 216, y + 32, readings, muted);
}

static const char *const direction_names[STICK_DIRECTIONS] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};

static void draw_return_status(uint32_t *pixels, const StickTest *test, int y)
{
    const char *warning = NULL;
    if (!test->reached_center)
        warning = "No llegó al centro";
    else if (!test->return_peak)
        warning = "Tope sin confirmar";
    draw_center_result(pixels, y, "Vuelta al centro", &test->after_release, warning);
}

static void draw_direction_scores(uint32_t *pixels, const StickTest *test, int top, int height)
{
    for (int i = 0; i < STICK_DIRECTIONS; ++i) {
        int x = 216 + (i % 4) * 60;
        int y = top + (i / 4) * (height + 4);
        bool reached = (test->reached & (1u << i)) != 0;
        round_box(pixels, x, y, 56, height, 5, reached ? RGB(0x203e38) : well);
        int baseline = y + (height + 9) / 2;
        draw_text(pixels, &small_font, x + 4, baseline, direction_names[i], reached ? hold_green : muted);
        char score[12] = "--";
        if (test->range_peak[i])
            snprintf(score, sizeof score, "%u%%", (unsigned int)test->range_peak[i]);
        draw_text(pixels, &button_font, x + 52 - text_width(&button_font, score), baseline, score,
                  reached ? hold_green : text);
    }
}

static void draw_stick_timer(uint32_t *pixels, uint32_t elapsed, unsigned int seconds, int y)
{
    char timer[24];
    unsigned int remaining = seconds * 10 - elapsed / 100000;
    snprintf(timer, sizeof timer, "Midiendo  %u,%u s", remaining / 10, remaining % 10);
    draw_text(pixels, &label_font, 216, y, timer, mint);
    draw_meter(pixels, 216, y + 10, 236, elapsed / (seconds * 10000), mint);
}

void stick_test_draw(uint32_t *pixels, const Panel *panel, const StickTest *test)
{
    draw_background(pixels);
    draw_text(pixels, &large_font, 16, 35, "Stick analógico", text);
    card_box(pixels, 16, 49, 177, 195);
    card_box(pixels, 204, 49, 260, 195);
    draw_stick_map(pixels, panel, test);

    const char *confirm = NULL;
    if (test->phase <= STICK_REST) {
        draw_text(pixels, &label_font, 216, 72, "1/3  Reposo", blue);
        draw_text(pixels, &small_font, 216, 96, "Deja el stick sin tocar.", text);
        if (test->phase == STICK_READY) {
            confirm = "Empezar";
        } else if (test->phase == STICK_SETTLE) {
            draw_text(pixels, &label_font, 216, 147, "Preparando...", mint);
        } else {
            draw_stick_timer(pixels, test->elapsed_us, STICK_REST_SECONDS, 147);
            draw_center_result(pixels, 189, "Reposo", &test->rest, NULL);
        }
    } else if (test->phase < STICK_RANGE_READY) {
        draw_text(pixels, &label_font, 216, 72, "2/3  Vuelta al centro", blue);
        if (test->phase == STICK_RETURN_TARGET) {
            draw_text(pixels, &small_font, 216, 96, "Empuja hasta el tope en un eje X o Y.", text);
            draw_text(pixels, &small_font, 216, 113, "Elige arriba, abajo, izquierda o derecha.", muted);
            confirm = "Ya lo solté";
        } else if (test->phase == STICK_RETURN_RELEASE) {
            char limit[48];
            snprintf(limit, sizeof limit, "Tope %s detectado. Suelta el stick.",
                     direction_names[test->return_direction]);
            draw_text(pixels, &label_font, 216, 97, limit, mint);
            draw_text(pixels, &small_font, 216, 117, "Si se queda atascado, pulsa X al soltar.", muted);
            confirm = "Ya lo solté";
        } else {
            draw_text(pixels, &small_font, 216, 96, "Deja que el stick vuelva solo al centro.", text);
            draw_stick_timer(pixels, test->elapsed_us, STICK_RETURN_SECONDS, 133);
        }
        draw_center_result(pixels, 184, "Reposo", &test->rest, NULL);
    } else if (test->phase == STICK_RANGE_READY) {
        draw_text(pixels, &label_font, 216, 72, "3/3  Recorrido por el borde", blue);
        draw_text(pixels, &small_font, 216, 97, "Da una vuelta lenta por todo el borde.", text);
        draw_return_status(pixels, test, 140);
        confirm = "Empezar recorrido";
    } else {
        unsigned int count = reached_directions(test);
        bool results = test->phase == STICK_RESULTS;
        draw_text(pixels, &label_font, 216, 72, results ? "Resultado del stick" : "3/3  Recorrido por el borde", blue);
        char reached[45];
        snprintf(reached, sizeof reached, "%u/%u direcciones  ·  Objetivo %u%%", count,
                 (unsigned int)STICK_DIRECTIONS, (unsigned int)STICK_RANGE_TARGET);
        uint32_t range_color = count == STICK_DIRECTIONS ? hold_green : muted;
        if (results) {
            draw_center_result(pixels, 92, "Reposo", &test->rest, NULL);
            draw_return_status(pixels, test, 143);
            draw_text(pixels, &small_font, 216, 193, reached, range_color);
            draw_direction_scores(pixels, test, 198, 18);
        } else {
            draw_text(pixels, &small_font, 216, 97, "Da una vuelta lenta por todo el borde.", text);
            draw_text(pixels, &small_font, 216, 122, reached, range_color);
            draw_direction_scores(pixels, test, 133, 23);
        }
        confirm = results ? "Repetir" : "Ver resultado";
    }
    if (confirm) {
        draw_text(pixels, &label_font, 16, 260, "X", blue);
        draw_text(pixels, &small_font, 30, 260, confirm, muted);
    }
    draw_text(pixels, &label_font, 319, 260, "Círculo", blue);
    draw_text(pixels, &small_font, 370, 260, "Volver al panel", muted);
}
