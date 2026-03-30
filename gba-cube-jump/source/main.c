#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_VCOUNT (*(volatile uint16_t *)0x04000006)
#define REG_KEYINPUT (*(volatile uint16_t *)0x04000130)
#define REG_DMA3SAD (*(volatile const void **)0x040000D4)
#define REG_DMA3DAD (*(volatile void **)0x040000D8)
#define REG_DMA3CNT (*(volatile uint32_t *)0x040000DC)

#define MODE3 0x0003
#define BG2_ENABLE 0x0400
#define DMA_ENABLE (1u << 31)

#define KEY_A (1u << 0)
#define KEY_B (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START (1u << 3)
#define KEY_RIGHT (1u << 4)
#define KEY_LEFT (1u << 5)
#define KEY_UP (1u << 6)
#define KEY_DOWN (1u << 7)

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 160
#define FRAME_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT)

#define MAX_COMMAND_LEN 63
#define MAX_OUTPUT_LINES 8
#define MAX_LINE_CHARS 34
#define MAX_EDITOR_LEN 512
#define PAINT_W 152
#define PAINT_H 88

#define FONT_W 5
#define FONT_H 7

#define RGB5(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

static volatile uint16_t *const video_buffer = (volatile uint16_t *)0x06000000;
static uint16_t back_buffer[FRAME_PIXELS] __attribute__((section(".ewram")));
static uint16_t gui_cache[FRAME_PIXELS] __attribute__((section(".ewram")));
static uint16_t *draw_buffer = back_buffer;
static volatile uint8_t *const save_ram = (volatile uint8_t *)0x0E000000;

typedef enum {
    APP_SPLASH,
    APP_TERMINAL,
    APP_EDITOR,
    APP_GUI
} AppMode;

typedef enum {
    KEY_INSERT_CHAR,
    KEY_INSERT_TEXT,
    KEY_SPACE,
    KEY_BACKSPACE,
    KEY_ENTER,
    KEY_CLEAR,
    KEY_COMMAND
} VirtualKeyType;

typedef enum {
    ICON_NONE,
    ICON_ROMS,
    ICON_COMMANDS,
    ICON_SETTINGS,
    ICON_PAINT,
    ICON_CALC,
    ICON_CURSOR,
    ICON_JUMP
} DesktopIcon;

typedef enum {
    WINDOW_NONE,
    WINDOW_OSINFO,
    WINDOW_ROMS,
    WINDOW_COMMANDS,
    WINDOW_SETTINGS,
    WINDOW_PAINT,
    WINDOW_CALC,
    WINDOW_CURSOR,
    WINDOW_JUMP
} GuiWindow;

typedef struct {
    uint16_t background;
    uint16_t panel;
    uint16_t panel_alt;
    uint16_t text;
    uint16_t accent;
    const char *name;
} Theme;

typedef struct {
    const char *label;
    VirtualKeyType type;
    char ch;
    const char *text;
} VirtualKey;

typedef struct {
    DesktopIcon icon;
    int x;
    int y;
    const char *label;
    bool is_folder;
} IconInfo;

typedef struct {
    const char *name;
    uint16_t color;
} PaintColor;

typedef struct {
    const char *label;
    char action;
} CalcButton;

typedef struct {
    const char *label;
    DesktopIcon icon;
} StartMenuItem;

typedef struct {
    AppMode mode;
    Theme theme;
    uint16_t keys;
    uint16_t previous_keys;
    int keyboard_row;
    int keyboard_col;
    char command[MAX_COMMAND_LEN + 1];
    int command_len;
    char output[MAX_OUTPUT_LINES][MAX_LINE_CHARS + 1];
    int output_count;
    char editor[MAX_EDITOR_LEN + 1];
    int editor_len;
    int cursor_x;
    int cursor_y;
    int cursor_fx;
    int cursor_fy;
    int cursor_vx;
    int cursor_vy;
    DesktopIcon selected_icon;
    DesktopIcon last_clicked_icon;
    int click_timer;
    int osinfo_click_timer;
    int osinfo_scroll;
    GuiWindow gui_window;
    uint8_t paint_canvas[PAINT_H][PAINT_W];
    uint8_t paint_color_index;
    int paint_saved_timer;
    int calc_value;
    int calc_stored_value;
    char calc_operator;
    int calc_input;
    int calc_result_timer;
    int runner_y;
    int runner_vy;
    int runner_obstacle_x;
    int runner_obstacle_w;
    int runner_spawn_timer;
    int runner_score;
    int runner_survival_timer;
    bool runner_alive;
    uint32_t rand_seed;
    int splash_timer;
    int frame_tick;
    int cpu_usage;
    int gpu_usage;
    int ram_usage;
    int usage_update_tick;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
    int window_restore_x;
    int window_restore_y;
    int window_restore_w;
    int window_restore_h;
    bool window_fullscreen;
    bool dragging_window;
    int drag_offset_x;
    int drag_offset_y;
    bool gui_dirty;
    uint8_t cursor_style;
    bool start_menu_open;
} AppState;

static const Theme THEME_BLUE = {RGB5(5, 8, 18), RGB5(9, 12, 23), RGB5(14, 18, 28), RGB5(31, 31, 31), RGB5(12, 24, 31), "BLUE"};
static const Theme THEME_RED = {RGB5(18, 6, 6), RGB5(24, 10, 10), RGB5(28, 15, 15), RGB5(31, 31, 31), RGB5(31, 23, 9), "RED"};
static const Theme THEME_GREEN = {RGB5(4, 12, 6), RGB5(8, 18, 10), RGB5(13, 23, 14), RGB5(31, 31, 31), RGB5(25, 31, 12), "GREEN"};
static const Theme THEME_WHITE = {RGB5(29, 29, 29), RGB5(24, 24, 24), RGB5(20, 20, 20), RGB5(1, 1, 1), RGB5(6, 10, 24), "WHITE"};
static const Theme THEME_BLACK = {RGB5(1, 1, 1), RGB5(4, 4, 4), RGB5(8, 8, 8), RGB5(31, 31, 31), RGB5(31, 0, 0), "BLACK"};
static const Theme THEME_YELLOW = {RGB5(28, 25, 8), RGB5(24, 21, 5), RGB5(20, 17, 3), RGB5(1, 1, 1), RGB5(31, 10, 0), "YELLOW"};
static const Theme THEME_CYAN = {RGB5(6, 24, 24), RGB5(5, 18, 18), RGB5(3, 14, 14), RGB5(31, 31, 31), RGB5(31, 31, 10), "CYAN"};
static const Theme THEME_MAGENTA = {RGB5(24, 6, 24), RGB5(18, 5, 18), RGB5(12, 3, 12), RGB5(31, 31, 31), RGB5(31, 24, 10), "MAGENTA"};

static const VirtualKey KEYBOARD[5][8] = {
    {
        {"A", KEY_INSERT_CHAR, 'A', 0}, {"B", KEY_INSERT_CHAR, 'B', 0}, {"C", KEY_INSERT_CHAR, 'C', 0}, {"D", KEY_INSERT_CHAR, 'D', 0},
        {"E", KEY_INSERT_CHAR, 'E', 0}, {"F", KEY_INSERT_CHAR, 'F', 0}, {"G", KEY_INSERT_CHAR, 'G', 0}, {"H", KEY_INSERT_CHAR, 'H', 0},
    },
    {
        {"I", KEY_INSERT_CHAR, 'I', 0}, {"J", KEY_INSERT_CHAR, 'J', 0}, {"K", KEY_INSERT_CHAR, 'K', 0}, {"L", KEY_INSERT_CHAR, 'L', 0},
        {"M", KEY_INSERT_CHAR, 'M', 0}, {"N", KEY_INSERT_CHAR, 'N', 0}, {"O", KEY_INSERT_CHAR, 'O', 0}, {"P", KEY_INSERT_CHAR, 'P', 0},
    },
    {
        {"Q", KEY_INSERT_CHAR, 'Q', 0}, {"R", KEY_INSERT_CHAR, 'R', 0}, {"S", KEY_INSERT_CHAR, 'S', 0}, {"T", KEY_INSERT_CHAR, 'T', 0},
        {"U", KEY_INSERT_CHAR, 'U', 0}, {"V", KEY_INSERT_CHAR, 'V', 0}, {"W", KEY_INSERT_CHAR, 'W', 0}, {"X", KEY_INSERT_CHAR, 'X', 0},
    },
    {
        {"Y", KEY_INSERT_CHAR, 'Y', 0}, {"Z", KEY_INSERT_CHAR, 'Z', 0}, {"SPC", KEY_SPACE, 0, 0}, {"BK", KEY_BACKSPACE, 0, 0},
        {"ENT", KEY_ENTER, 0, 0}, {"CLR", KEY_CLEAR, 0, 0}, {".", KEY_INSERT_CHAR, '.', 0}, {"-", KEY_INSERT_CHAR, '-', 0},
    },
    {
        {"RED", KEY_INSERT_TEXT, 0, "RED"}, {"GRN", KEY_INSERT_TEXT, 0, "GREEN"}, {"BLU", KEY_INSERT_TEXT, 0, "BLUE"}, {"WHT", KEY_INSERT_TEXT, 0, "WHITE"},
        {"BLK", KEY_INSERT_TEXT, 0, "BLACK"}, {"YEL", KEY_INSERT_TEXT, 0, "YELLOW"}, {"CYN", KEY_INSERT_TEXT, 0, "CYAN"}, {"CMD", KEY_COMMAND, 0, 0},
    }
};

static const IconInfo DESKTOP_ICONS[7] = {
    {ICON_ROMS, 20, 42, "ROMS", true},
    {ICON_COMMANDS, 74, 42, "COMMANDS", true},
    {ICON_SETTINGS, 128, 42, "SETTINGS", true},
    {ICON_JUMP, 182, 42, "JUMP", false},
    {ICON_CALC, 38, 104, "CALC", false},
    {ICON_PAINT, 96, 104, "PAINT", false},
    {ICON_CURSOR, 154, 104, "CURSOR", false},
};

static const PaintColor PAINT_COLORS[6] = {
    {"BLK", RGB5(0, 0, 0)},
    {"RED", RGB5(31, 4, 4)},
    {"BLU", RGB5(6, 18, 31)},
    {"GRN", RGB5(5, 22, 8)},
    {"YEL", RGB5(31, 27, 5)},
    {"MAG", RGB5(27, 6, 27)},
};

static const CalcButton CALC_BUTTONS[4][4] = {
    {{"7", '7'}, {"8", '8'}, {"9", '9'}, {"+", '+'}},
    {{"4", '4'}, {"5", '5'}, {"6", '6'}, {"-", '-'}},
    {{"1", '1'}, {"2", '2'}, {"3", '3'}, {"=", '='}},
    {{"C", 'C'}, {"0", '0'}, {"*", '*'}, {"/", '/'}},
};

static const StartMenuItem START_MENU_ITEMS[7] = {
    {"ROMS", ICON_ROMS},
    {"COMMANDS", ICON_COMMANDS},
    {"SETTINGS", ICON_SETTINGS},
    {"JUMPRUNNER", ICON_JUMP},
    {"PAINT", ICON_PAINT},
    {"CALCULATOR", ICON_CALC},
    {"CURSORS", ICON_CURSOR},
};

static inline uint16_t key_state(void) {
    return (uint16_t)(~REG_KEYINPUT & 0x03FFu);
}

static inline bool key_hit(const AppState *app, uint16_t key) {
    return (app->keys & key) && !(app->previous_keys & key);
}

static inline bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static inline char upper_char(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static const uint8_t *glyph_for_char(char c) {
    static const uint8_t fallback[7] = {0x1E, 0x02, 0x04, 0x08, 0x00, 0x08, 0x00};
    static const uint8_t space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t period[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
    static const uint8_t comma[7] = {0, 0, 0, 0, 0, 0x0C, 0x08};
    static const uint8_t apostrophe[7] = {0x0C, 0x0C, 0x08, 0, 0, 0, 0};
    static const uint8_t colon[7] = {0, 0x0C, 0x0C, 0, 0x0C, 0x0C, 0};
    static const uint8_t dash[7] = {0, 0, 0, 0x1E, 0, 0, 0};
    static const uint8_t slash[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0, 0};
    static const uint8_t lparen[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    static const uint8_t rparen[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
    static const uint8_t atsign[7] = {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E};
    static const uint8_t greater[7] = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10};
    static const uint8_t digit_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t digit_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t digit_2[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    static const uint8_t digit_3[7] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E};
    static const uint8_t digit_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t digit_5[7] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E};
    static const uint8_t digit_6[7] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t digit_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t digit_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t digit_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
    static const uint8_t letter_a[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t letter_b[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t letter_c[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t letter_d[7] = {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C};
    static const uint8_t letter_e[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t letter_f[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t letter_g[7] = {0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0E};
    static const uint8_t letter_h[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t letter_i[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    static const uint8_t letter_j[7] = {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E};
    static const uint8_t letter_k[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t letter_l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t letter_m[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t letter_n[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t letter_o[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t letter_p[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t letter_q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t letter_r[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t letter_s[7] = {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E};
    static const uint8_t letter_t[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t letter_u[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t letter_v[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t letter_w[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t letter_x[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t letter_y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t letter_z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};

    switch (upper_char(c)) {
        case ' ': return space;
        case '.': return period;
        case ',': return comma;
        case '\'': return apostrophe;
        case ':': return colon;
        case '-': return dash;
        case '/': return slash;
        case '(': return lparen;
        case ')': return rparen;
        case '@': return atsign;
        case '>': return greater;
        case '0': return digit_0;
        case '1': return digit_1;
        case '2': return digit_2;
        case '3': return digit_3;
        case '4': return digit_4;
        case '5': return digit_5;
        case '6': return digit_6;
        case '7': return digit_7;
        case '8': return digit_8;
        case '9': return digit_9;
        case 'A': return letter_a;
        case 'B': return letter_b;
        case 'C': return letter_c;
        case 'D': return letter_d;
        case 'E': return letter_e;
        case 'F': return letter_f;
        case 'G': return letter_g;
        case 'H': return letter_h;
        case 'I': return letter_i;
        case 'J': return letter_j;
        case 'K': return letter_k;
        case 'L': return letter_l;
        case 'M': return letter_m;
        case 'N': return letter_n;
        case 'O': return letter_o;
        case 'P': return letter_p;
        case 'Q': return letter_q;
        case 'R': return letter_r;
        case 'S': return letter_s;
        case 'T': return letter_t;
        case 'U': return letter_u;
        case 'V': return letter_v;
        case 'W': return letter_w;
        case 'X': return letter_x;
        case 'Y': return letter_y;
        case 'Z': return letter_z;
        default: return fallback;
    }
}

static void wait_for_vblank(void) {
    while (REG_VCOUNT >= 160) {
    }
    while (REG_VCOUNT < 160) {
    }
}

static void present_frame(void) {
    REG_DMA3CNT = 0;
    REG_DMA3SAD = back_buffer;
    REG_DMA3DAD = (void *)video_buffer;
    REG_DMA3CNT = DMA_ENABLE | FRAME_PIXELS;
}

static void copy_buffer(uint16_t *dst, const uint16_t *src) {
    uint32_t *dst32 = (uint32_t *)dst;
    const uint32_t *src32 = (const uint32_t *)src;
    for (int i = 0; i < FRAME_PIXELS / 2; ++i) {
        dst32[i] = src32[i];
    }
}

static void fill_screen(uint16_t color) {
    uint32_t packed = (uint32_t)color | ((uint32_t)color << 16);
    uint32_t *dst = (uint32_t *)draw_buffer;
    for (int i = 0; i < FRAME_PIXELS / 2; ++i) {
        dst[i] = packed;
    }
}

static void draw_rect(int x, int y, int width, int height, uint16_t color) {
    if (width <= 0 || height <= 0) {
        return;
    }

    int start_x = x < 0 ? 0 : x;
    int start_y = y < 0 ? 0 : y;
    int end_x = x + width;
    int end_y = y + height;

    if (end_x > SCREEN_WIDTH) {
        end_x = SCREEN_WIDTH;
    }
    if (end_y > SCREEN_HEIGHT) {
        end_y = SCREEN_HEIGHT;
    }

    for (int py = start_y; py < end_y; ++py) {
        uint16_t *row = &draw_buffer[py * SCREEN_WIDTH];
        for (int px = start_x; px < end_x; ++px) {
            row[px] = color;
        }
    }
}

static void draw_frame(int x, int y, int width, int height, uint16_t border, uint16_t fill) {
    draw_rect(x, y, width, height, border);
    draw_rect(x + 1, y + 1, width - 2, height - 2, fill);
}

static void draw_char(int x, int y, char c, uint16_t color, int scale) {
    const uint8_t *glyph = glyph_for_char(c);

    for (int row = 0; row < FONT_H; ++row) {
        for (int col = 0; col < FONT_W; ++col) {
            if (glyph[row] & (1u << (FONT_W - 1 - col))) {
                draw_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint16_t color, int scale) {
    while (*text) {
        draw_char(x, y, *text, color, scale);
        x += (FONT_W + 1) * scale;
        ++text;
    }
}

static void clear_output(AppState *app) {
    app->output_count = 0;
    for (int i = 0; i < MAX_OUTPUT_LINES; ++i) {
        app->output[i][0] = '\0';
    }
}

static void append_output(AppState *app, const char *text) {
    if (app->output_count == MAX_OUTPUT_LINES) {
        for (int i = 1; i < MAX_OUTPUT_LINES; ++i) {
            memcpy(app->output[i - 1], app->output[i], MAX_LINE_CHARS + 1);
        }
        app->output_count = MAX_OUTPUT_LINES - 1;
    }

    int len = 0;
    while (text[len] && len < MAX_LINE_CHARS) {
        app->output[app->output_count][len] = upper_char(text[len]);
        ++len;
    }
    app->output[app->output_count][len] = '\0';
    app->output_count += 1;
}

static void show_boot_text(AppState *app) {
    clear_output(app);
    append_output(app, "GBA LINUX ALIKE TERMINAL");
    append_output(app, "TYPE INFO");
    append_output(app, "TYPE COLOUR BLUE");
    append_output(app, "TYPE TYPEWRITE");
    append_output(app, "TYPE GUI");
}

static bool starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (upper_char(*text) != upper_char(*prefix)) {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

static bool string_equals(const char *a, const char *b) {
    while (*a && *b) {
        if (upper_char(*a) != upper_char(*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static const Theme *theme_from_name(const char *name) {
    if (string_equals(name, "BLUE")) return &THEME_BLUE;
    if (string_equals(name, "RED")) return &THEME_RED;
    if (string_equals(name, "GREEN")) return &THEME_GREEN;
    if (string_equals(name, "WHITE")) return &THEME_WHITE;
    if (string_equals(name, "BLACK")) return &THEME_BLACK;
    if (string_equals(name, "YELLOW")) return &THEME_YELLOW;
    if (string_equals(name, "CYAN")) return &THEME_CYAN;
    if (string_equals(name, "MAGENTA")) return &THEME_MAGENTA;
    return 0;
}

static void insert_text(char *buffer, int *length, int max_length, const char *text) {
    while (*text && *length < max_length) {
        buffer[*length] = upper_char(*text);
        *length += 1;
        ++text;
    }
    buffer[*length] = '\0';
}

static void show_info(AppState *app) {
    clear_output(app);
    append_output(app, "SYSTEM: GAME BOY ADVANCE");
    append_output(app, "CPU: ARM7TDMI 16.78MHZ");
    append_output(app, "SCREEN: 240X160 LCD");
    append_output(app, "WRAM: 256KB + 32KB");
    append_output(app, "VRAM: 96KB");
    append_output(app, "INPUT: DPAD A B L R");
    append_output(app, "SOUND: PSG + DIRECT");
    append_output(app, "THIS APP USES MODE 3");
}

static void reset_window_geometry(AppState *app) {
    app->window_x = 22;
    app->window_y = 20;
    app->window_w = 196;
    app->window_h = 118;
    app->window_restore_x = app->window_x;
    app->window_restore_y = app->window_y;
    app->window_restore_w = app->window_w;
    app->window_restore_h = app->window_h;
    app->window_fullscreen = false;
    app->dragging_window = false;
}

static GuiWindow icon_to_window(DesktopIcon icon) {
    switch (icon) {
        case ICON_ROMS: return WINDOW_ROMS;
        case ICON_COMMANDS: return WINDOW_COMMANDS;
        case ICON_SETTINGS: return WINDOW_SETTINGS;
        case ICON_PAINT: return WINDOW_PAINT;
        case ICON_CALC: return WINDOW_CALC;
        case ICON_CURSOR: return WINDOW_CURSOR;
        case ICON_JUMP: return WINDOW_JUMP;
        default: return WINDOW_NONE;
    }
}

static void open_desktop_icon(AppState *app, DesktopIcon icon) {
    app->gui_window = icon_to_window(icon);
    app->selected_icon = icon;
    app->start_menu_open = false;
    reset_window_geometry(app);
    if (app->gui_window == WINDOW_OSINFO) {
        app->osinfo_scroll = 0;
        app->window_x = 12;
        app->window_y = 18;
        app->window_w = 216;
        app->window_h = 122;
    } else if (app->gui_window == WINDOW_JUMP) {
        app->runner_y = 0;
        app->runner_vy = 0;
        app->runner_obstacle_x = 200;
        app->runner_obstacle_w = 10;
        app->runner_spawn_timer = 110;
        app->runner_score = 0;
        app->runner_survival_timer = 0;
        app->runner_alive = true;
    }
    app->gui_dirty = true;
}

static void save_paint_to_sram(const AppState *app) {
    static const uint8_t header[8] = {'G', 'G', 'P', '1', PAINT_W & 0xFF, PAINT_H & 0xFF, 6, 0};
    for (int i = 0; i < 8; ++i) {
        save_ram[i] = header[i];
    }

    save_ram[8] = app->paint_color_index;
    int offset = 16;
    for (int y = 0; y < PAINT_H; ++y) {
        for (int x = 0; x < PAINT_W; ++x) {
            save_ram[offset++] = app->paint_canvas[y][x];
        }
    }
}

static void load_paint_from_sram(AppState *app) {
    if (save_ram[0] != 'G' || save_ram[1] != 'G' || save_ram[2] != 'P' || save_ram[3] != '1') {
        return;
    }

    int offset = 16;
    app->paint_color_index = save_ram[8] % 6;
    for (int y = 0; y < PAINT_H; ++y) {
        for (int x = 0; x < PAINT_W; ++x) {
            app->paint_canvas[y][x] = save_ram[offset++];
        }
    }
}

static void run_command(AppState *app) {
    char *command = app->command;
    while (*command == ' ') {
        ++command;
    }

    if (*command == '\0') {
        clear_output(app);
        append_output(app, "NO COMMAND ENTERED");
        return;
    }

    if (string_equals(command, "INFO")) {
        show_info(app);
        return;
    }

    if (string_equals(command, "TYPEWRITE")) {
        app->mode = APP_EDITOR;
        clear_output(app);
        append_output(app, "TYPEWRITE OPEN");
        append_output(app, "USE CMD TO RETURN");
        return;
    }

    if (string_equals(command, "GUI")) {
        app->mode = APP_GUI;
        app->cursor_fx = 120 << 4;
        app->cursor_fy = 80 << 4;
        app->cursor_x = 120;
        app->cursor_y = 80;
        app->cursor_vx = 0;
        app->cursor_vy = 0;
        app->selected_icon = ICON_NONE;
        app->last_clicked_icon = ICON_NONE;
        app->click_timer = 0;
        app->gui_window = WINDOW_NONE;
        reset_window_geometry(app);
        app->gui_dirty = true;
        clear_output(app);
        append_output(app, "GUI OPEN");
        append_output(app, "DOUBLE TAP A TO OPEN");
        return;
    }

    if (starts_with(command, "COLOUR ") || starts_with(command, "COLOR ")) {
        char *argument = command + (upper_char(command[3]) == 'O' && upper_char(command[4]) == 'R' ? 6 : 7);
        while (*argument == ' ') {
            ++argument;
        }

        const Theme *theme = theme_from_name(argument);
        clear_output(app);
        if (theme) {
            app->theme = *theme;
            append_output(app, "BACKGROUND UPDATED");
            append_output(app, theme->name);
        } else {
            append_output(app, "UNKNOWN COLOUR");
            append_output(app, "TRY RED BLUE GREEN");
            append_output(app, "WHITE BLACK YELLOW");
            append_output(app, "CYAN OR MAGENTA");
        }
        return;
    }

    clear_output(app);
    append_output(app, "UNKNOWN COMMAND");
    append_output(app, "USE INFO COLOUR TYPEWRITE");
    append_output(app, "OR GUI");
}

static void handle_keyboard_navigation(AppState *app) {
    if (key_hit(app, KEY_LEFT) && app->keyboard_col > 0) {
        app->keyboard_col -= 1;
    }
    if (key_hit(app, KEY_RIGHT) && app->keyboard_col < 7) {
        app->keyboard_col += 1;
    }
    if (key_hit(app, KEY_UP) && app->keyboard_row > 0) {
        app->keyboard_row -= 1;
    }
    if (key_hit(app, KEY_DOWN) && app->keyboard_row < 4) {
        app->keyboard_row += 1;
    }
}

static void editor_backspace(AppState *app) {
    if (app->editor_len > 0) {
        app->editor_len -= 1;
        app->editor[app->editor_len] = '\0';
    }
}

static void return_to_terminal(AppState *app, const char *line1, const char *line2) {
    app->mode = APP_TERMINAL;
    app->gui_window = WINDOW_NONE;
    app->dragging_window = false;
    clear_output(app);
    append_output(app, line1);
    append_output(app, line2);
}

static void apply_virtual_key(AppState *app) {
    const VirtualKey *key = &KEYBOARD[app->keyboard_row][app->keyboard_col];

    switch (key->type) {
        case KEY_INSERT_CHAR:
            if (app->mode == APP_EDITOR) {
                if (app->editor_len < MAX_EDITOR_LEN) {
                    app->editor[app->editor_len++] = key->ch;
                    app->editor[app->editor_len] = '\0';
                }
            } else if (app->command_len < MAX_COMMAND_LEN) {
                app->command[app->command_len++] = key->ch;
                app->command[app->command_len] = '\0';
            }
            break;
        case KEY_INSERT_TEXT:
            if (app->mode == APP_EDITOR) {
                insert_text(app->editor, &app->editor_len, MAX_EDITOR_LEN, key->text);
            } else {
                insert_text(app->command, &app->command_len, MAX_COMMAND_LEN, key->text);
            }
            break;
        case KEY_SPACE:
            if (app->mode == APP_EDITOR) {
                if (app->editor_len < MAX_EDITOR_LEN) {
                    app->editor[app->editor_len++] = ' ';
                    app->editor[app->editor_len] = '\0';
                }
            } else if (app->command_len < MAX_COMMAND_LEN) {
                app->command[app->command_len++] = ' ';
                app->command[app->command_len] = '\0';
            }
            break;
        case KEY_BACKSPACE:
            if (app->mode == APP_EDITOR) {
                editor_backspace(app);
            } else if (app->command_len > 0) {
                app->command_len -= 1;
                app->command[app->command_len] = '\0';
            }
            break;
        case KEY_ENTER:
            if (app->mode == APP_EDITOR) {
                if (app->editor_len < MAX_EDITOR_LEN) {
                    app->editor[app->editor_len++] = '\n';
                    app->editor[app->editor_len] = '\0';
                }
            } else {
                run_command(app);
                app->command_len = 0;
                app->command[0] = '\0';
            }
            break;
        case KEY_CLEAR:
            if (app->mode == APP_EDITOR) {
                app->editor_len = 0;
                app->editor[0] = '\0';
            } else {
                app->command_len = 0;
                app->command[0] = '\0';
            }
            break;
        case KEY_COMMAND:
            if (app->mode == APP_EDITOR) {
                return_to_terminal(app, "RETURNED TO TERMINAL", "TEXT KEPT IN MEMORY");
            }
            break;
    }
}

static DesktopIcon icon_under_cursor(const AppState *app) {
    for (int i = 0; i < 6; ++i) {
        const IconInfo *icon = &DESKTOP_ICONS[i];
        if (point_in_rect(app->cursor_x, app->cursor_y, icon->x, icon->y, 30, 32)) {
            return icon->icon;
        }
    }
    return ICON_NONE;
}

static void update_gui_cursor(AppState *app) {
    const int accel = 8;
    const int drag = 5;
    const int max_speed = 28;

    if (app->keys & KEY_LEFT) app->cursor_vx -= accel;
    if (app->keys & KEY_RIGHT) app->cursor_vx += accel;
    if (app->keys & KEY_UP) app->cursor_vy -= accel;
    if (app->keys & KEY_DOWN) app->cursor_vy += accel;

    if (!(app->keys & (KEY_LEFT | KEY_RIGHT))) {
        if (app->cursor_vx > 0) {
            app->cursor_vx -= drag;
            if (app->cursor_vx < 0) app->cursor_vx = 0;
        } else if (app->cursor_vx < 0) {
            app->cursor_vx += drag;
            if (app->cursor_vx > 0) app->cursor_vx = 0;
        }
    }

    if (!(app->keys & (KEY_UP | KEY_DOWN))) {
        if (app->cursor_vy > 0) {
            app->cursor_vy -= drag;
            if (app->cursor_vy < 0) app->cursor_vy = 0;
        } else if (app->cursor_vy < 0) {
            app->cursor_vy += drag;
            if (app->cursor_vy > 0) app->cursor_vy = 0;
        }
    }

    if (app->cursor_vx > max_speed) app->cursor_vx = max_speed;
    if (app->cursor_vx < -max_speed) app->cursor_vx = -max_speed;
    if (app->cursor_vy > max_speed) app->cursor_vy = max_speed;
    if (app->cursor_vy < -max_speed) app->cursor_vy = -max_speed;

    app->cursor_fx += app->cursor_vx;
    app->cursor_fy += app->cursor_vy;

    if (app->cursor_fx < 0) {
        app->cursor_fx = 0;
        app->cursor_vx = 0;
    }
    if (app->cursor_fy < 0) {
        app->cursor_fy = 0;
        app->cursor_vy = 0;
    }
    if (app->cursor_fx > ((SCREEN_WIDTH - 8) << 4)) {
        app->cursor_fx = (SCREEN_WIDTH - 8) << 4;
        app->cursor_vx = 0;
    }
    if (app->cursor_fy > ((SCREEN_HEIGHT - 8) << 4)) {
        app->cursor_fy = (SCREEN_HEIGHT - 8) << 4;
        app->cursor_vy = 0;
    }

    app->cursor_x = app->cursor_fx >> 4;
    app->cursor_y = app->cursor_fy >> 4;
}

static void toggle_fullscreen(AppState *app) {
    if (app->window_fullscreen) {
        app->window_x = app->window_restore_x;
        app->window_y = app->window_restore_y;
        app->window_w = app->window_restore_w;
        app->window_h = app->window_restore_h;
        app->window_fullscreen = false;
    } else {
        app->window_restore_x = app->window_x;
        app->window_restore_y = app->window_y;
        app->window_restore_w = app->window_w;
        app->window_restore_h = app->window_h;
        app->window_x = 8;
        app->window_y = 18;
        app->window_w = 224;
        app->window_h = 124;
        app->window_fullscreen = true;
    }
}

static int canvas_x(const AppState *app) {
    return app->window_x + (app->window_w - PAINT_W) / 2;
}

static int canvas_y(const AppState *app) {
    return app->window_y + 20 + (app->window_h - 28 - PAINT_H) / 2;
}

static void paint_at_cursor(AppState *app, uint8_t value) {
    int left = canvas_x(app);
    int top = canvas_y(app);

    if (app->gui_window != WINDOW_PAINT) {
        return;
    }

    if (!point_in_rect(app->cursor_x, app->cursor_y, left, top, PAINT_W, PAINT_H)) {
        return;
    }

    int px = app->cursor_x - left;
    int py = app->cursor_y - top;

    for (int oy = -1; oy <= 1; ++oy) {
        int y = py + oy;
        if (y < 0 || y >= PAINT_H) {
            continue;
        }
        for (int ox = -1; ox <= 1; ++ox) {
            int x = px + ox;
            if (x < 0 || x >= PAINT_W) {
                continue;
            }
            app->paint_canvas[y][x] = value;
        }
    }
}

static void calculator_clear(AppState *app) {
    app->calc_value = 0;
    app->calc_stored_value = 0;
    app->calc_operator = 0;
    app->calc_input = 0;
}

static void calculator_push_digit(AppState *app, int digit) {
    if (app->calc_input < 9999) {
        app->calc_input = app->calc_input * 10 + digit;
    }
}

static void calculator_apply_pending(AppState *app) {
    int rhs = app->calc_input;
    if (app->calc_operator == 0) {
        app->calc_value = rhs;
    } else if (app->calc_operator == '+') {
        app->calc_value = app->calc_stored_value + rhs;
    } else if (app->calc_operator == '-') {
        app->calc_value = app->calc_stored_value - rhs;
    } else if (app->calc_operator == '*') {
        app->calc_value = app->calc_stored_value * rhs;
    } else if (app->calc_operator == '/') {
        app->calc_value = rhs == 0 ? 0 : app->calc_stored_value / rhs;
    }

    app->calc_stored_value = app->calc_value;
    app->calc_input = 0;
    app->calc_result_timer = 45;
}

static void calculator_set_operator(AppState *app, char op) {
    if (app->calc_operator == 0) {
        app->calc_value = app->calc_input;
        app->calc_stored_value = app->calc_input;
        app->calc_input = 0;
    } else {
        calculator_apply_pending(app);
    }
    app->calc_operator = op;
}

static uint32_t next_random(AppState *app) {
    app->rand_seed = app->rand_seed * 1664525u + 1013904223u;
    return app->rand_seed;
}

static void update_jump_runner(AppState *app) {
    const int ground_y = 64;

    if (!app->runner_alive) {
        return;
    }

    app->runner_vy += 1;
    if (app->runner_vy > 5) {
        app->runner_vy = 5;
    }
    app->runner_y += app->runner_vy;
    if (app->runner_y > 0) {
        app->runner_y = 0;
        app->runner_vy = 0;
    }

    app->runner_obstacle_x -= 3;
    if (app->runner_obstacle_x + app->runner_obstacle_w < 0) {
        app->runner_spawn_timer = 80 + (int)(next_random(app) % 80u);
        app->runner_obstacle_w = 8 + (int)(next_random(app) % 8u);
        app->runner_obstacle_x = 210 + app->runner_spawn_timer;
    }

    app->runner_survival_timer += 1;
    if (app->runner_survival_timer >= 300) {
        app->runner_survival_timer = 0;
        app->runner_score += 100;
    }

    int player_left = 24;
    int player_right = 34;
    int player_top = ground_y - 10 + app->runner_y;
    int player_bottom = ground_y + app->runner_y;
    int obstacle_left = app->runner_obstacle_x;
    int obstacle_right = app->runner_obstacle_x + app->runner_obstacle_w;
    int obstacle_top = ground_y - 10;

    if (player_right > obstacle_left &&
        player_left < obstacle_right &&
        player_bottom > obstacle_top &&
        player_top < ground_y) {
        app->runner_alive = false;
    }
}

static void update_usage_stats(AppState *app) {
    int previous_cpu = app->cpu_usage;
    int previous_gpu = app->gpu_usage;
    int previous_ram = app->ram_usage;
    int cpu = 12;
    int gpu = 18;
    int ram = 21;

    if (app->mode == APP_GUI) {
        cpu += 8;
        gpu += 10;
        ram += 8;

        int speed = (app->cursor_vx >= 0 ? app->cursor_vx : -app->cursor_vx) +
                    (app->cursor_vy >= 0 ? app->cursor_vy : -app->cursor_vy);
        cpu += speed / 3;
        gpu += speed / 5;

        if (app->gui_window != WINDOW_NONE) {
            cpu += 6;
            gpu += 6;
            ram += 10;
        }
        if (app->dragging_window) {
            cpu += 10;
            gpu += 8;
        }
        if (app->window_fullscreen) {
            gpu += 5;
        }
        if (app->gui_window == WINDOW_PAINT) {
            cpu += 10;
            gpu += 14;
            ram += 12;
            if (app->keys & (KEY_A | KEY_B)) {
                cpu += 7;
                gpu += 8;
            }
        }
    } else if (app->mode == APP_EDITOR) {
        cpu += 6;
        gpu += 6;
        ram += 10;
    } else if (app->mode == APP_TERMINAL) {
        cpu += 4;
        gpu += 5;
        ram += 6;
    }

    cpu += (app->usage_update_tick % 6);
    gpu += ((app->usage_update_tick * 2) % 5);
    ram += app->editor_len / 32;

    app->cpu_usage = cpu > 99 ? 99 : cpu;
    app->gpu_usage = gpu > 99 ? 99 : gpu;
    app->ram_usage = ram > 99 ? 99 : ram;

    if (app->mode == APP_GUI &&
        (app->cpu_usage != previous_cpu || app->gpu_usage != previous_gpu || app->ram_usage != previous_ram)) {
        app->gui_dirty = true;
    }
}

static void update_app(AppState *app) {
    app->previous_keys = app->keys;
    app->keys = key_state();
    app->frame_tick += 1;

    if (app->paint_saved_timer > 0) {
        app->paint_saved_timer -= 1;
        if (app->paint_saved_timer == 0 && app->mode == APP_GUI) {
            app->gui_dirty = true;
        }
    }
    if (app->calc_result_timer > 0) {
        app->calc_result_timer -= 1;
    }

    if (app->mode == APP_SPLASH) {
        if (app->splash_timer > 0) {
            app->splash_timer -= 1;
        }
        if (app->splash_timer == 0 || key_hit(app, KEY_A) || key_hit(app, KEY_START)) {
            app->mode = APP_TERMINAL;
            show_boot_text(app);
        }
        if ((app->frame_tick & 7) == 0) {
            app->usage_update_tick += 1;
            update_usage_stats(app);
        }
        return;
    }

    if (app->mode == APP_GUI) {
        update_gui_cursor(app);

        if (app->click_timer > 0) {
            app->click_timer -= 1;
            if (app->click_timer == 0) {
                app->last_clicked_icon = ICON_NONE;
            }
        }
        if (app->osinfo_click_timer > 0) {
            app->osinfo_click_timer -= 1;
        }

        if (app->dragging_window && !(app->keys & KEY_A)) {
            app->dragging_window = false;
        }

        if (app->dragging_window) {
            app->window_x = app->cursor_x - app->drag_offset_x;
            app->window_y = app->cursor_y - app->drag_offset_y;

            if (app->window_x < 8) app->window_x = 8;
            if (app->window_y < 18) app->window_y = 18;
            if (app->window_x + app->window_w > 232) app->window_x = 232 - app->window_w;
            if (app->window_y + app->window_h > 142) app->window_y = 142 - app->window_h;
            app->gui_dirty = true;
        }

        if (app->gui_window == WINDOW_PAINT && !app->dragging_window) {
            if (app->keys & KEY_A) {
                paint_at_cursor(app, (uint8_t)(app->paint_color_index + 1));
                app->gui_dirty = true;
            }
            if (app->keys & KEY_B) {
                paint_at_cursor(app, 0);
                app->gui_dirty = true;
            }
        } else if (app->gui_window == WINDOW_JUMP) {
            update_jump_runner(app);
            app->gui_dirty = true;
        } else if (app->gui_window == WINDOW_OSINFO) {
            if (key_hit(app, KEY_UP) && app->osinfo_scroll > 0) {
                app->osinfo_scroll -= 1;
                app->gui_dirty = true;
            }
            if (key_hit(app, KEY_DOWN) && app->osinfo_scroll < 4) {
                app->osinfo_scroll += 1;
                app->gui_dirty = true;
            }
        }
    } else {
        handle_keyboard_navigation(app);
    }

    if (app->mode != APP_GUI && key_hit(app, KEY_A)) {
        apply_virtual_key(app);
    }

    if (app->mode == APP_GUI && key_hit(app, KEY_A)) {
        if (app->gui_window == WINDOW_NONE) {
            if (point_in_rect(app->cursor_x, app->cursor_y, 6, 146, 46, 12)) {
                app->start_menu_open = !app->start_menu_open;
                app->gui_dirty = true;
                return;
            }

            if (point_in_rect(app->cursor_x, app->cursor_y, 58, 146, 74, 12)) {
                if (app->osinfo_click_timer > 0) {
                    app->osinfo_click_timer = 0;
                    open_desktop_icon(app, ICON_NONE);
                    app->gui_window = WINDOW_OSINFO;
                    app->start_menu_open = false;
                    app->osinfo_scroll = 0;
                    app->window_x = 12;
                    app->window_y = 18;
                    app->window_w = 216;
                    app->window_h = 122;
                    app->gui_dirty = true;
                } else {
                    app->osinfo_click_timer = 20;
                }
                return;
            }

            if (app->start_menu_open) {
                int menu_x = 6;
                int menu_y = 42;
                for (int i = 0; i < 7; ++i) {
                    int item_y = menu_y + 12 + i * 12;
                    if (point_in_rect(app->cursor_x, app->cursor_y, menu_x + 6, item_y, 92, 12)) {
                        open_desktop_icon(app, START_MENU_ITEMS[i].icon);
                        return;
                    }
                }
                app->start_menu_open = false;
                app->gui_dirty = true;
                return;
            }

            DesktopIcon hovered = icon_under_cursor(app);
            if (hovered != ICON_NONE) {
                if (app->selected_icon == hovered &&
                    app->last_clicked_icon == hovered &&
                    app->click_timer > 0) {
                    app->last_clicked_icon = ICON_NONE;
                    app->click_timer = 0;
                    open_desktop_icon(app, hovered);
                } else {
                    app->selected_icon = hovered;
                    app->last_clicked_icon = hovered;
                    app->click_timer = 20;
                    app->gui_dirty = true;
                }
            } else {
                app->selected_icon = ICON_NONE;
                app->start_menu_open = false;
                app->gui_dirty = true;
            }
        } else {
            if (app->gui_window == WINDOW_JUMP) {
                if (app->runner_alive) {
                    if (app->runner_y == 0) {
                        app->runner_vy = -9;
                    }
                } else {
                    app->runner_y = 0;
                    app->runner_vy = 0;
                    app->runner_obstacle_x = 200;
                    app->runner_obstacle_w = 10;
                    app->runner_spawn_timer = 110;
                    app->runner_score = 0;
                    app->runner_survival_timer = 0;
                    app->runner_alive = true;
                }
                app->gui_dirty = true;
                return;
            } else if (app->gui_window == WINDOW_OSINFO) {
                int track_x = app->window_x + app->window_w - 18;
                int track_y = app->window_y + 24;
                int track_h = app->window_h - 36;
                if (point_in_rect(app->cursor_x, app->cursor_y, track_x, track_y, 8, track_h)) {
                    int local_y = app->cursor_y - track_y;
                    int max_scroll = 4;
                    app->osinfo_scroll = (local_y * (max_scroll + 1)) / track_h;
                    if (app->osinfo_scroll > max_scroll) app->osinfo_scroll = max_scroll;
                    app->gui_dirty = true;
                }
            }

            int full_x = app->window_x + app->window_w - 14;
            int full_y = app->window_y + 3;
            if (app->gui_window != WINDOW_JUMP && point_in_rect(app->cursor_x, app->cursor_y, full_x, full_y, 8, 8)) {
                toggle_fullscreen(app);
                app->gui_dirty = true;
            } else if (app->gui_window != WINDOW_JUMP &&
                       point_in_rect(app->cursor_x, app->cursor_y, app->window_x + 4, app->window_y + 2, app->window_w - 20, 12)) {
                if (!app->window_fullscreen) {
                    app->dragging_window = true;
                    app->drag_offset_x = app->cursor_x - app->window_x;
                    app->drag_offset_y = app->cursor_y - app->window_y;
                }
            } else if (app->gui_window == WINDOW_PAINT) {
                int palette_y = app->window_y + 24;
                for (int i = 0; i < 6; ++i) {
                    int swatch_x = app->window_x + 12 + i * 18;
                    if (point_in_rect(app->cursor_x, app->cursor_y, swatch_x, palette_y, 14, 10)) {
                        app->paint_color_index = (uint8_t)i;
                        app->gui_dirty = true;
                    }
                }

                int save_x = app->window_x + app->window_w - 42;
                int save_y = app->window_y + app->window_h - 14;
                if (point_in_rect(app->cursor_x, app->cursor_y, save_x, save_y, 30, 10)) {
                    save_paint_to_sram(app);
                    app->paint_saved_timer = 90;
                    app->gui_dirty = true;
                }
            } else if (app->gui_window == WINDOW_CALC) {
                int grid_x = app->window_x + 18;
                int grid_y = app->window_y + 42;
                for (int row = 0; row < 4; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        int bx = grid_x + col * 28;
                        int by = grid_y + row * 16;
                        if (point_in_rect(app->cursor_x, app->cursor_y, bx, by, 22, 12)) {
                            char action = CALC_BUTTONS[row][col].action;
                            if (action >= '0' && action <= '9') {
                                calculator_push_digit(app, action - '0');
                            } else if (action == 'C') {
                                calculator_clear(app);
                            } else if (action == '=') {
                                calculator_apply_pending(app);
                                app->calc_operator = 0;
                            } else if (action == '+') {
                                calculator_set_operator(app, '+');
                            } else if (action == '-') {
                                calculator_set_operator(app, '-');
                            } else if (action == '*') {
                                calculator_set_operator(app, '*');
                            } else if (action == '/') {
                                calculator_set_operator(app, '/');
                            }
                            app->gui_dirty = true;
                        }
                    }
                }
            } else if (app->gui_window == WINDOW_CURSOR) {
                int base_x = app->window_x + 20;
                int base_y = app->window_y + 42;
                for (int i = 0; i < 4; ++i) {
                    int bx = base_x + i * 38;
                    if (point_in_rect(app->cursor_x, app->cursor_y, bx, base_y, 28, 34)) {
                        app->cursor_style = (uint8_t)i;
                        app->gui_dirty = true;
                    }
                }
            }
        }
    }

    if (key_hit(app, KEY_B)) {
        if (app->mode == APP_EDITOR) {
            editor_backspace(app);
        } else if (app->mode == APP_GUI) {
            if (app->gui_window == WINDOW_JUMP) {
                app->gui_window = WINDOW_NONE;
                app->gui_dirty = true;
            } else if (app->gui_window != WINDOW_PAINT && app->gui_window != WINDOW_NONE) {
                app->gui_window = WINDOW_NONE;
                app->dragging_window = false;
                app->gui_dirty = true;
            } else if (app->start_menu_open) {
                app->start_menu_open = false;
                app->gui_dirty = true;
            } else if (app->gui_window == WINDOW_NONE) {
                app->selected_icon = ICON_NONE;
                return_to_terminal(app, "RETURNED TO TERMINAL", "GUI CLOSED");
            }
        } else if (app->command_len > 0) {
            app->command_len -= 1;
            app->command[app->command_len] = '\0';
        }
    }

    if (key_hit(app, KEY_START)) {
        if (app->mode == APP_EDITOR) {
            return_to_terminal(app, "RETURNED TO TERMINAL", "TEXT KEPT IN MEMORY");
        } else if (app->mode == APP_GUI) {
            if (app->gui_window != WINDOW_NONE) {
                app->gui_window = WINDOW_NONE;
                app->dragging_window = false;
                app->gui_dirty = true;
            } else if (app->start_menu_open) {
                app->start_menu_open = false;
                app->gui_dirty = true;
            } else {
                return_to_terminal(app, "RETURNED TO TERMINAL", "GUI CLOSED");
            }
        } else {
            run_command(app);
            app->command_len = 0;
            app->command[0] = '\0';
        }
    }

    if (app->mode == APP_GUI && key_hit(app, KEY_SELECT) && app->gui_window == WINDOW_PAINT) {
        memset(app->paint_canvas, 0, sizeof(app->paint_canvas));
        app->gui_dirty = true;
    } else if (key_hit(app, KEY_SELECT)) {
        show_boot_text(app);
    }

    if ((app->frame_tick & 7) == 0) {
        app->usage_update_tick += 1;
        update_usage_stats(app);
    }
}

static void draw_terminal_output(const AppState *app) {
    draw_frame(6, 6, 228, 18, app->theme.accent, app->theme.panel);
    draw_text(12, 12, ">", app->theme.accent, 1);
    draw_text(22, 12, app->command, app->theme.text, 1);

    draw_frame(6, 28, 228, 64, app->theme.accent, app->theme.panel_alt);
    for (int i = 0; i < app->output_count; ++i) {
        draw_text(12, 34 + i * 7, app->output[i], app->theme.text, 1);
    }
}

static void draw_editor_output(const AppState *app) {
    char rendered[MAX_OUTPUT_LINES][MAX_LINE_CHARS + 1];
    int row = 0;
    int col = 0;

    for (int i = 0; i < MAX_OUTPUT_LINES; ++i) {
        rendered[i][0] = '\0';
    }

    for (int i = 0; i < app->editor_len && row < MAX_OUTPUT_LINES; ++i) {
        char ch = app->editor[i];
        if (ch == '\n') {
            rendered[row][col] = '\0';
            row += 1;
            col = 0;
            continue;
        }
        if (col == MAX_LINE_CHARS) {
            rendered[row][col] = '\0';
            row += 1;
            col = 0;
            if (row >= MAX_OUTPUT_LINES) {
                break;
            }
        }
        rendered[row][col++] = upper_char(ch);
        rendered[row][col] = '\0';
    }

    draw_frame(6, 6, 228, 18, app->theme.accent, app->theme.panel);
    draw_text(12, 12, "TYPEWRITE", app->theme.text, 1);
    draw_text(106, 12, "CMD=EXIT", app->theme.accent, 1);

    draw_frame(6, 28, 228, 64, app->theme.accent, app->theme.panel_alt);
    for (int i = 0; i < MAX_OUTPUT_LINES; ++i) {
        draw_text(12, 34 + i * 7, rendered[i], app->theme.text, 1);
    }
}

static void draw_keyboard(const AppState *app) {
    const int key_w = 26;
    const int key_h = 11;
    const int start_x = 8;
    const int start_y = 98;
    const int gap_x = 2;
    const int gap_y = 2;

    draw_frame(6, 96, 228, 58, app->theme.accent, app->theme.panel);

    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 8; ++col) {
            const VirtualKey *key = &KEYBOARD[row][col];
            int x = start_x + col * (key_w + gap_x);
            int y = start_y + row * (key_h + gap_y);
            bool selected = row == app->keyboard_row && col == app->keyboard_col;
            uint16_t border = selected ? app->theme.text : app->theme.accent;
            uint16_t fill = selected ? app->theme.accent : app->theme.panel_alt;
            uint16_t text_color = selected ? app->theme.background : app->theme.text;

            draw_frame(x, y, key_w, key_h, border, fill);
            draw_text(x + 4, y + 2, key->label, text_color, 1);
        }
    }
}

static void draw_folder(int x, int y, const char *label) {
    draw_rect(x + 4, y + 4, 30, 20, RGB5(10, 10, 10));
    draw_rect(x + 4, y, 12, 6, RGB5(18, 15, 4));
    draw_frame(x, y + 4, 30, 20, RGB5(18, 15, 4), RGB5(25, 21, 7));
    draw_text(x - 4, y + 28, label, RGB5(1, 1, 1), 1);
}

static void draw_paint_icon(int x, int y, const char *label) {
    draw_frame(x, y, 30, 24, RGB5(3, 3, 3), RGB5(26, 26, 26));
    draw_rect(x + 4, y + 4, 6, 6, RGB5(31, 4, 4));
    draw_rect(x + 12, y + 4, 6, 6, RGB5(6, 24, 31));
    draw_rect(x + 20, y + 4, 6, 6, RGB5(25, 24, 5));
    draw_rect(x + 8, y + 12, 14, 6, RGB5(10, 20, 10));
    draw_text(x, y + 28, label, RGB5(1, 1, 1), 1);
}

static void draw_calc_icon(int x, int y, const char *label) {
    draw_frame(x, y, 30, 24, RGB5(3, 3, 3), RGB5(20, 20, 20));
    draw_rect(x + 6, y + 4, 18, 5, RGB5(18, 28, 18));
    draw_rect(x + 6, y + 12, 4, 4, RGB5(31, 31, 31));
    draw_rect(x + 12, y + 12, 4, 4, RGB5(31, 31, 31));
    draw_rect(x + 18, y + 12, 4, 4, RGB5(31, 31, 31));
    draw_rect(x + 12, y + 18, 10, 2, RGB5(31, 31, 31));
    draw_text(x + 1, y + 28, label, RGB5(1, 1, 1), 1);
}

static void draw_cursor_icon(int x, int y, const char *label) {
    draw_frame(x, y, 30, 24, RGB5(3, 3, 3), RGB5(22, 22, 26));
    draw_rect(x + 7, y + 4, 2, 10, RGB5(31, 31, 31));
    draw_rect(x + 9, y + 5, 2, 8, RGB5(31, 31, 31));
    draw_rect(x + 11, y + 6, 2, 6, RGB5(31, 31, 31));
    draw_rect(x + 13, y + 7, 2, 4, RGB5(31, 31, 31));
    draw_rect(x + 18, y + 8, 6, 6, RGB5(6, 18, 31));
    draw_text(x - 1, y + 28, label, RGB5(1, 1, 1), 1);
}

static void draw_jump_icon(int x, int y, const char *label) {
    draw_frame(x, y, 30, 24, RGB5(3, 3, 3), RGB5(24, 24, 24));
    draw_rect(x + 4, y + 16, 22, 2, RGB5(10, 20, 10));
    draw_rect(x + 7, y + 8, 8, 8, RGB5(31, 31, 31));
    draw_rect(x + 18, y + 10, 6, 6, RGB5(31, 4, 4));
    draw_text(x + 2, y + 28, label, RGB5(1, 1, 1), 1);
}

static void draw_cursor(int x, int y) {
    draw_rect(x, y, 2, 8, RGB5(0, 0, 0));
    draw_rect(x + 2, y + 1, 2, 6, RGB5(0, 0, 0));
    draw_rect(x + 4, y + 2, 2, 4, RGB5(0, 0, 0));
    draw_rect(x + 6, y + 3, 2, 2, RGB5(0, 0, 0));
}

static void draw_cursor_style(int style, int x, int y, uint16_t color) {
    switch (style) {
        case 1:
            draw_rect(x + 3, y, 2, 10, color);
            draw_rect(x, y + 4, 8, 2, color);
            break;
        case 2:
            draw_rect(x, y, 2, 8, color);
            draw_rect(x + 2, y + 1, 2, 6, color);
            draw_rect(x + 4, y + 2, 2, 4, color);
            draw_rect(x + 6, y + 3, 2, 2, color);
            draw_rect(x + 3, y + 6, 2, 4, color);
            break;
        case 3:
            draw_rect(x + 1, y + 1, 6, 6, color);
            draw_rect(x + 3, y, 2, 8, color);
            break;
        default:
            draw_cursor(x, y);
            break;
    }
}

static void draw_usage_bar(int x, int y, int value, uint16_t fill) {
    draw_frame(x, y, 30, 6, RGB5(2, 2, 2), RGB5(28, 28, 28));
    draw_rect(x + 2, y + 2, (value * 26) / 100, 2, fill);
}

static void draw_usage_panel(const AppState *app) {
    draw_frame(148, 2, 88, 28, RGB5(2, 2, 2), RGB5(23, 23, 23));
    draw_text(152, 5, "CPU", RGB5(1, 1, 1), 1);
    draw_text(152, 12, "GPU", RGB5(1, 1, 1), 1);
    draw_text(152, 19, "RAM", RGB5(1, 1, 1), 1);
    draw_usage_bar(170, 5, app->cpu_usage, RGB5(6, 24, 10));
    draw_usage_bar(170, 12, app->gpu_usage, RGB5(8, 16, 31));
    draw_usage_bar(170, 19, app->ram_usage, RGB5(28, 20, 4));
}

static void draw_goose(int x, int y, bool blink) {
    draw_rect(x + 12, y + 10, 26, 20, RGB5(31, 31, 31));
    draw_rect(x + 30, y + 4, 16, 14, RGB5(31, 31, 31));
    draw_rect(x + 42, y + 12, 14, 6, RGB5(31, 18, 3));
    draw_rect(x + 18, y + 28, 4, 10, RGB5(31, 18, 3));
    draw_rect(x + 30, y + 28, 4, 10, RGB5(31, 18, 3));
    draw_rect(x + 8, y + 14, 8, 8, RGB5(31, 31, 31));
    if (blink) {
        draw_rect(x + 36, y + 10, 4, 1, RGB5(1, 1, 1));
    } else {
        draw_rect(x + 36, y + 9, 3, 3, RGB5(1, 1, 1));
    }
}

static void draw_taskbar_goose(int x, int y) {
    draw_rect(x + 8, y + 3, 8, 6, RGB5(31, 31, 31));
    draw_rect(x + 14, y + 1, 5, 5, RGB5(31, 31, 31));
    draw_rect(x + 18, y + 4, 5, 2, RGB5(31, 18, 3));
    draw_rect(x + 10, y + 8, 2, 4, RGB5(31, 18, 3));
    draw_rect(x + 14, y + 8, 2, 4, RGB5(31, 18, 3));
    draw_rect(x + 7, y + 5, 3, 3, RGB5(31, 31, 31));
}

static void draw_taskbar(const AppState *app) {
    draw_rect(0, 144, SCREEN_WIDTH, 16, RGB5(18, 18, 18));
    draw_rect(0, 143, SCREEN_WIDTH, 1, RGB5(10, 10, 10));

    draw_frame(6, 146, 46, 12, app->start_menu_open ? RGB5(31, 31, 0) : RGB5(1, 1, 1), RGB5(24, 24, 24));
    draw_taskbar_goose(8, 146);
    draw_text(24, 149, "START", RGB5(31, 31, 31), 1);

    draw_frame(58, 146, 74, 12, RGB5(1, 1, 1), RGB5(24, 24, 24));
    draw_text(66, 149, "OSINFO", RGB5(31, 31, 31), 1);
    if (app->gui_window == WINDOW_NONE) {
        draw_text(136, 149, "DESK", RGB5(31, 31, 0), 1);
    } else if (app->gui_window == WINDOW_PAINT) {
        draw_text(136, 149, "PNT", RGB5(31, 31, 0), 1);
    } else if (app->gui_window == WINDOW_CALC) {
        draw_text(136, 149, "CALC", RGB5(31, 31, 0), 1);
    } else if (app->gui_window == WINDOW_CURSOR) {
        draw_text(136, 149, "CUR", RGB5(31, 31, 0), 1);
    } else if (app->gui_window == WINDOW_JUMP) {
        draw_text(136, 149, "JUMP", RGB5(31, 31, 0), 1);
    } else {
        draw_text(136, 149, "APP", RGB5(31, 31, 0), 1);
    }
}

static void draw_start_menu(const AppState *app) {
    if (!app->start_menu_open) {
        return;
    }

    int menu_x = 6;
    int menu_y = 42;
    draw_frame(menu_x, menu_y, 104, 100, RGB5(2, 2, 2), RGB5(24, 24, 24));
    draw_rect(menu_x + 2, menu_y + 2, 100, 12, RGB5(31, 31, 0));
    draw_text(menu_x + 8, menu_y + 5, "GOOSEGBA MENU", RGB5(1, 1, 1), 1);

    for (int i = 0; i < 7; ++i) {
        int item_y = menu_y + 12 + i * 12;
        bool hovered = point_in_rect(app->cursor_x, app->cursor_y, menu_x + 6, item_y, 92, 12);
        draw_frame(menu_x + 6, item_y, 92, 12, hovered ? RGB5(31, 31, 0) : RGB5(1, 1, 1), hovered ? RGB5(20, 20, 28) : RGB5(28, 28, 28));
        draw_text(menu_x + 12, item_y + 3, START_MENU_ITEMS[i].label, RGB5(31, 31, 31), 1);
    }
}

static void draw_splash(const AppState *app) {
    fill_screen(RGB5(12, 20, 31));
    draw_rect(0, 116, SCREEN_WIDTH, 44, RGB5(8, 18, 8));
    draw_rect(20, 24, 200, 72, RGB5(24, 27, 31));
    draw_frame(24, 28, 192, 64, RGB5(31, 31, 31), RGB5(6, 10, 18));
    draw_goose(76, 46, ((app->frame_tick / 20) & 1) != 0);
    draw_text(52, 36, "GOOSEGBA", RGB5(31, 31, 0), 2);
    draw_text(54, 104, "HONKING INTO THE BIOS", RGB5(31, 31, 31), 1);
    draw_text(70, 122, "PRESS A TO SKIP", RGB5(1, 1, 1), 1);
}

static void draw_window_shell(const AppState *app, const char *title, const char *hint) {
    draw_frame(app->window_x, app->window_y, app->window_w, app->window_h, RGB5(2, 2, 2), RGB5(27, 27, 27));
    draw_rect(app->window_x + 2, app->window_y + 2, app->window_w - 4, 14, RGB5(4, 4, 4));
    draw_text(app->window_x + 8, app->window_y + 5, title, RGB5(31, 31, 31), 1);
    draw_text(app->window_x + 64, app->window_y + 5, hint, RGB5(1, 1, 1), 1);
    draw_frame(app->window_x + app->window_w - 14, app->window_y + 3, 8, 8, RGB5(1, 1, 1), RGB5(20, 20, 20));
    if (app->window_fullscreen) {
        draw_rect(app->window_x + app->window_w - 12, app->window_y + 5, 4, 4, RGB5(1, 1, 1));
    } else {
        draw_rect(app->window_x + app->window_w - 11, app->window_y + 4, 3, 3, RGB5(1, 1, 1));
    }
    draw_rect(app->window_x + 8, app->window_y + 20, app->window_w - 16, app->window_h - 28, RGB5(30, 30, 30));
}

static void blit_paint_canvas(const AppState *app, int left, int top) {
    uint16_t *dst = &draw_buffer[top * SCREEN_WIDTH + left];
    for (int y = 0; y < PAINT_H; ++y) {
        uint16_t *row = dst + y * SCREEN_WIDTH;
        for (int x = 0; x < PAINT_W; ++x) {
            uint8_t index = app->paint_canvas[y][x];
            row[x] = index == 0 ? RGB5(31, 31, 31) : PAINT_COLORS[index - 1].color;
        }
    }
}

static void draw_gui_window(const AppState *app) {
    static const char *osinfo_lines[11] = {
        "GOOSEGBA VER 0.45",
        "(30/03/26 BUILD)",
        "PLEASE NOTE THAT SOME",
        "(MOST) FEATURES ARE",
        "CURRENTLY IN BETA",
        "SO THINGS MAY FEEL",
        "'INCOMPLETE' OR",
        "'UNFINISHED'.",
        "THIS PROJECT TRIES",
        "TO SHOW A GBA OS.",
        "EMAIL OLLYT88@OUTLOOK.COM",
    };

    switch (app->gui_window) {
        case WINDOW_ROMS:
            draw_window_shell(app, "ROMS", "DRAG OR FULL");
            draw_text(app->window_x + 16, app->window_y + 28, "CUBEJUMP.GBA", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 42, "PAINT SAVE USES SRAM", RGB5(1, 1, 1), 1);
            break;
        case WINDOW_OSINFO:
            draw_window_shell(app, "OSINFO", "DRAG OR FULL");
            for (int i = 0; i < 7; ++i) {
                int line = app->osinfo_scroll + i;
                if (line < 11) {
                    uint16_t color = line == 10 ? RGB5(31, 31, 0) : RGB5(1, 1, 1);
                    draw_text(app->window_x + 16, app->window_y + 28 + i * 14, osinfo_lines[line], color, 1);
                }
            }
            draw_frame(app->window_x + app->window_w - 18, app->window_y + 24, 8, app->window_h - 36, RGB5(1, 1, 1), RGB5(20, 20, 20));
            draw_rect(app->window_x + app->window_w - 16, app->window_y + 28 + app->osinfo_scroll * 12, 4, 18, RGB5(31, 31, 31));
            break;
        case WINDOW_COMMANDS:
            draw_window_shell(app, "COMMANDS", "DRAG OR FULL");
            draw_text(app->window_x + 16, app->window_y + 28, "INFO", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 40, "COLOUR <NAME>", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 52, "TYPEWRITE", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 64, "GUI", RGB5(1, 1, 1), 1);
            break;
        case WINDOW_SETTINGS:
            draw_window_shell(app, "SETTINGS", "DRAG OR FULL");
            draw_text(app->window_x + 16, app->window_y + 28, "WINDOWS: DRAG TITLE BAR", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 40, "PAINT: SELECT CLEARS", RGB5(1, 1, 1), 1);
            draw_text(app->window_x + 16, app->window_y + 52, "PAINT: START CLOSES", RGB5(1, 1, 1), 1);
            break;
        case WINDOW_PAINT: {
            int left = canvas_x(app);
            int top = canvas_y(app);
            draw_window_shell(app, "PAINT", "DRAG OR FULL");

            for (int i = 0; i < 6; ++i) {
                int swatch_x = app->window_x + 12 + i * 18;
                int swatch_y = app->window_y + 24;
                uint16_t border = app->paint_color_index == i ? RGB5(31, 31, 31) : RGB5(2, 2, 2);
                draw_frame(swatch_x, swatch_y, 14, 10, border, PAINT_COLORS[i].color);
            }

            draw_frame(left - 2, top - 2, PAINT_W + 4, PAINT_H + 4, RGB5(2, 2, 2), RGB5(31, 31, 31));
            blit_paint_canvas(app, left, top);

            draw_frame(app->window_x + app->window_w - 42, app->window_y + app->window_h - 14, 30, 10, RGB5(1, 1, 1), RGB5(12, 18, 12));
            draw_text(app->window_x + app->window_w - 36, app->window_y + app->window_h - 12, "SAVE", RGB5(31, 31, 31), 1);
            draw_text(app->window_x + 12, app->window_y + app->window_h - 12, "A DRAW B ERASE", RGB5(1, 1, 1), 1);
            if (app->paint_saved_timer > 0) {
                draw_text(app->window_x + app->window_w - 92, app->window_y + app->window_h - 12, "SAVED", RGB5(5, 24, 8), 1);
            }
            break;
        }
        case WINDOW_CALC: {
            draw_window_shell(app, "CALCULATOR", "DRAG OR FULL");
            draw_frame(app->window_x + 18, app->window_y + 24, 116, 12, RGB5(1, 1, 1), RGB5(18, 24, 18));
            char number_buf[16];
            int shown = app->calc_input;
            if (app->calc_operator == 0 && app->calc_input == 0) {
                shown = app->calc_value;
            }
            int pos = 14;
            number_buf[pos] = '\0';
            int value = shown;
            bool negative = value < 0;
            if (negative) value = -value;
            do {
                number_buf[--pos] = (char)('0' + (value % 10));
                value /= 10;
            } while (value > 0 && pos > 0);
            if (negative && pos > 0) number_buf[--pos] = '-';
            draw_text(app->window_x + 24, app->window_y + 27, &number_buf[pos], RGB5(1, 1, 1), 1);
            if (app->calc_operator) {
                char op_buf[2] = {app->calc_operator, '\0'};
                draw_text(app->window_x + 118, app->window_y + 27, op_buf, RGB5(31, 31, 0), 1);
            }

            int grid_x = app->window_x + 18;
            int grid_y = app->window_y + 42;
            for (int row = 0; row < 4; ++row) {
                for (int col = 0; col < 4; ++col) {
                    int bx = grid_x + col * 28;
                    int by = grid_y + row * 16;
                    draw_frame(bx, by, 22, 12, RGB5(1, 1, 1), RGB5(22, 22, 22));
                    draw_text(bx + 7, by + 3, CALC_BUTTONS[row][col].label, RGB5(31, 31, 31), 1);
                }
            }
            break;
        }
        case WINDOW_CURSOR: {
            draw_window_shell(app, "CURSOR", "DRAG OR FULL");
            draw_text(app->window_x + 18, app->window_y + 28, "CHOOSE A POINTER", RGB5(1, 1, 1), 1);
            int base_x = app->window_x + 20;
            int base_y = app->window_y + 42;
            for (int i = 0; i < 4; ++i) {
                uint16_t border = app->cursor_style == i ? RGB5(31, 31, 0) : RGB5(1, 1, 1);
                draw_frame(base_x + i * 38, base_y, 28, 34, border, RGB5(26, 26, 26));
                draw_cursor_style(i, base_x + 10 + i * 38, base_y + 10, RGB5(1, 1, 1));
            }
            break;
        }
        case WINDOW_JUMP: {
            int left = app->window_x + 12;
            int top = app->window_y + 26;
            int ground_y = top + 64;
            draw_window_shell(app, "JUMPRUNNER BETA", "A JUMP B EXIT");
            draw_text(app->window_x + 18, app->window_y + 24, "SCORE", RGB5(1, 1, 1), 1);
            char score_buf[8];
            int pos = 7;
            int score = app->runner_score;
            score_buf[pos] = '\0';
            do {
                score_buf[--pos] = (char)('0' + (score % 10));
                score /= 10;
            } while (score > 0 && pos > 0);
            draw_text(app->window_x + 60, app->window_y + 24, &score_buf[pos], RGB5(31, 31, 0), 1);
            draw_rect(left, top, app->window_w - 24, 72, RGB5(22, 26, 30));
            draw_rect(left, ground_y, app->window_w - 24, 2, RGB5(10, 22, 10));
            draw_rect(left + 24, ground_y - 10 + app->runner_y, 10, 10, RGB5(31, 31, 31));
            draw_rect(left + app->runner_obstacle_x, ground_y - 10, app->runner_obstacle_w, 10, RGB5(31, 4, 4));
            if (!app->runner_alive) {
                draw_text(left + 42, top + 24, "CRASHED", RGB5(31, 31, 0), 1);
                draw_text(left + 24, top + 36, "PRESS A TO RETRY", RGB5(1, 1, 1), 1);
            }
            break;
        }
        default:
            break;
    }
}

static void draw_gui(const AppState *app) {
    fill_screen(RGB5(20, 20, 20));
    draw_rect(0, 0, SCREEN_WIDTH, 16, RGB5(24, 24, 24));
    draw_text(8, 4, "GBA DESKTOP", RGB5(1, 1, 1), 1);
    draw_text(92, 4, "GOOSE UI", RGB5(1, 1, 1), 1);
    draw_rect(0, 16, SCREEN_WIDTH, 1, RGB5(12, 12, 12));
    draw_usage_panel(app);

    for (int i = 0; i < 6; ++i) {
        const IconInfo *icon = &DESKTOP_ICONS[i];
        if (app->selected_icon == icon->icon) {
            draw_frame(icon->x - 4, icon->y - 4, 38, 42, RGB5(31, 31, 31), RGB5(16, 16, 20));
        }
        if (icon->is_folder) {
            draw_folder(icon->x, icon->y, icon->label);
        } else if (icon->icon == ICON_PAINT) {
            draw_paint_icon(icon->x, icon->y, icon->label);
        } else if (icon->icon == ICON_CALC) {
            draw_calc_icon(icon->x, icon->y, icon->label);
        } else if (icon->icon == ICON_JUMP) {
            draw_jump_icon(icon->x, icon->y, icon->label);
        } else {
            draw_cursor_icon(icon->x, icon->y, icon->label);
        }
    }

    if (app->gui_window != WINDOW_NONE) {
        draw_gui_window(app);
    }

    draw_start_menu(app);
    draw_taskbar(app);
}

static void render_app(AppState *app) {
    if (app->mode == APP_SPLASH) {
        draw_buffer = back_buffer;
        draw_splash(app);
        return;
    }
    if (app->mode == APP_GUI) {
        if (app->gui_dirty) {
            draw_buffer = back_buffer;
            draw_gui(app);
            copy_buffer(gui_cache, back_buffer);
            app->gui_dirty = false;
        } else {
            copy_buffer(back_buffer, gui_cache);
            draw_buffer = back_buffer;
        }
        draw_cursor_style(app->cursor_style, app->cursor_x, app->cursor_y, RGB5(0, 0, 0));
        return;
    }

    draw_buffer = back_buffer;
    fill_screen(app->theme.background);
    if (app->mode == APP_EDITOR) {
        draw_editor_output(app);
    } else {
        draw_terminal_output(app);
    }
    draw_keyboard(app);
}

static void init_app(AppState *app) {
    memset(app, 0, sizeof(*app));
    app->mode = APP_SPLASH;
    app->theme = THEME_BLUE;
    app->cursor_x = 120;
    app->cursor_y = 80;
    app->cursor_fx = 120 << 4;
    app->cursor_fy = 80 << 4;
    app->splash_timer = 180;
    app->cpu_usage = 8;
    app->gpu_usage = 12;
    app->ram_usage = 18;
    app->gui_dirty = true;
    app->cursor_style = 0;
    app->start_menu_open = false;
    app->rand_seed = 0x12345678u;
    reset_window_geometry(app);
    load_paint_from_sram(app);
}

int main(void) {
    AppState app;

    REG_DISPCNT = MODE3 | BG2_ENABLE;
    init_app(&app);
    render_app(&app);
    wait_for_vblank();
    present_frame();

    while (true) {
        update_app(&app);
        render_app(&app);
        wait_for_vblank();
        present_frame();
    }
}
