// Setup menu, driven entirely by the dial: turn to move, push to select,
// push and hold to go back. The list is on the left; the right pane previews
// the result live.

#include "bz_internal.h"

#include <stdio.h>
#include <string.h>

typedef enum {
    PAGE_ROOT,
    PAGE_LAYOUT,      // Picker: which mining page
    PAGE_METRICS,     // The slots of the current mining page
    PAGE_PICK_METRIC, // Picker: metric for one slot
    PAGE_DIGITS,      // Picker: big-number style
    PAGE_BRIGHTNESS,  // Picker
    PAGE_RESET,       // Confirm
} page_t;

enum { ROOT_LAYOUT, ROOT_METRICS, ROOT_DIGITS, ROOT_BRIGHTNESS, ROOT_RESET, ROOT_EXIT, ROOT_COUNT };

#define LIST_W 128
#define ROWS 6
#define ROW_H 9
#define LIST_Y 10

static struct {
    page_t page;
    int cursor;
    int top;          // First visible row
    int slot;         // Slot being edited in PAGE_PICK_METRIC
    int root_cursor;  // Where to return to
    int metrics_cursor;
    uint8_t original; // Value to restore if a picker is cancelled
} m;

// The picker lists real metrics first and "(EMPTY)" last.
static bz_metric_t metric_at(int index) {
    return (bz_metric_t)((index + 1) % BZ_M_COUNT);
}

static int metric_index(bz_metric_t metric) {
    return ((int)metric + BZ_M_COUNT - 1) % BZ_M_COUNT;
}

static bz_page_t mining_page(void) {
    return (bz_page_t)bz.settings.page;
}

static bool is_picker(page_t page) {
    return page == PAGE_LAYOUT || page == PAGE_PICK_METRIC || page == PAGE_DIGITS ||
           page == PAGE_BRIGHTNESS;
}

static int item_count(void) {
    switch (m.page) {
    case PAGE_ROOT: return ROOT_COUNT;
    case PAGE_LAYOUT: return BZ_PAGE_COUNT;
    case PAGE_METRICS: return 1 + bz_page_slot_count(mining_page());
    case PAGE_PICK_METRIC: return BZ_M_COUNT;
    case PAGE_DIGITS: return BZ_DIGITS_COUNT;
    case PAGE_BRIGHTNESS: return BZ_BRIGHTNESS_LEVELS;
    default: return 2;
    }
}

// The setting a picker edits.
static uint8_t *picker_target(void) {
    switch (m.page) {
    case PAGE_LAYOUT: return &bz.settings.page;
    case PAGE_PICK_METRIC: return &bz.settings.slots[mining_page()][m.slot];
    case PAGE_DIGITS: return &bz.settings.digits;
    default: return &bz.settings.brightness;
    }
}

static uint8_t picker_value(int index) {
    return m.page == PAGE_PICK_METRIC ? (uint8_t)metric_at(index) : (uint8_t)index;
}

static void go(page_t page, int cursor) {
    m.page = page;
    m.cursor = cursor;
    m.top = 0;
    if (is_picker(page)) {
        m.original = *picker_target();
        m.cursor = page == PAGE_PICK_METRIC ? metric_index((bz_metric_t)m.original) : m.original;
    }
}

void bz_menu_open(void) {
    memset(&m, 0, sizeof(m));
    m.metrics_cursor = 1;
    go(PAGE_ROOT, 0);
}

static void item_text(int index, char *label, size_t label_size, char *value, size_t value_size) {
    static const char *const digits[] = {"SOLID", "DOT MATRIX"};
    static const char *const levels[] = {"1 LOW", "2", "3", "4 HIGH"};
    *value = '\0';
    switch (m.page) {
    case PAGE_ROOT:
        switch (index) {
        case ROOT_LAYOUT:
            snprintf(label, label_size, "LAYOUT");
            snprintf(value, value_size, "%s", bz_page_name(mining_page()));
            break;
        case ROOT_METRICS:
            snprintf(label, label_size, "METRICS");
            snprintf(value, value_size, bz_page_slot_count(mining_page()) ? ">" : "N/A");
            break;
        case ROOT_DIGITS:
            snprintf(label, label_size, "BIG DIGITS");
            snprintf(value, value_size, "%s", bz.settings.digits ? "DOTS" : "SOLID");
            break;
        case ROOT_BRIGHTNESS:
            snprintf(label, label_size, "BRIGHTNESS");
            snprintf(value, value_size, "%d/%d", bz.settings.brightness + 1, BZ_BRIGHTNESS_LEVELS);
            break;
        case ROOT_RESET: snprintf(label, label_size, "RESET ALL"); break;
        default: snprintf(label, label_size, "EXIT"); break;
        }
        break;
    case PAGE_LAYOUT: snprintf(label, label_size, "%s", bz_page_name((bz_page_t)index)); break;
    case PAGE_METRICS:
        if (index == 0) {
            snprintf(label, label_size, "< BACK");
        } else {
            snprintf(label, label_size, "%s", bz_page_slot_name(mining_page(), index - 1));
            snprintf(value, value_size, "%s",
                     bz_metric_name((bz_metric_t)bz.settings.slots[mining_page()][index - 1]));
        }
        break;
    case PAGE_PICK_METRIC: snprintf(label, label_size, "%s", bz_metric_name(metric_at(index))); break;
    case PAGE_DIGITS: snprintf(label, label_size, "%s", digits[index]); break;
    case PAGE_BRIGHTNESS: snprintf(label, label_size, "%s", levels[index]); break;
    default: snprintf(label, label_size, "%s", index ? "YES, RESET" : "NO, KEEP"); break;
    }
    if (is_picker(m.page) && picker_value(index) == m.original) snprintf(value, value_size, "*");
}

static void back(void) {
    if (m.page == PAGE_PICK_METRIC) go(PAGE_METRICS, m.metrics_cursor);
    else go(PAGE_ROOT, m.root_cursor);
}

// Returns false to close the menu.
static bool select_item(void) {
    switch (m.page) {
    case PAGE_ROOT:
        m.root_cursor = m.cursor;
        switch (m.cursor) {
        case ROOT_LAYOUT: go(PAGE_LAYOUT, 0); break;
        case ROOT_METRICS:
            if (bz_page_slot_count(mining_page())) go(PAGE_METRICS, 1);
            break;
        case ROOT_DIGITS: go(PAGE_DIGITS, 0); break;
        case ROOT_BRIGHTNESS: go(PAGE_BRIGHTNESS, 0); break;
        case ROOT_RESET: go(PAGE_RESET, 0); break;
        default: return false;
        }
        break;
    case PAGE_METRICS:
        if (m.cursor == 0) {
            back();
        } else {
            m.metrics_cursor = m.cursor;
            m.slot = m.cursor - 1;
            go(PAGE_PICK_METRIC, 0);
        }
        break;
    case PAGE_RESET:
        if (m.cursor == 1) {
            bz_settings_defaults(&bz.settings);
            bz_settings_changed();
        }
        back();
        break;
    default: // Pickers already applied the choice while turning.
        back();
        break;
    }
    return true;
}

bool bz_menu_input(int steps, bz_press_t press) {
    if (steps) {
        int count = item_count();
        m.cursor += steps;
        if (m.cursor < 0) m.cursor = 0;
        if (m.cursor >= count) m.cursor = count - 1;
        if (is_picker(m.page)) {
            // Apply as the dial turns so the preview (and brightness) follow.
            *picker_target() = picker_value(m.cursor);
            bz_settings_changed();
        }
    }
    if (press == BZ_PRESS_HOLD) {
        if (m.page == PAGE_ROOT) return false;
        if (is_picker(m.page)) {
            *picker_target() = m.original; // Cancel
            bz_settings_changed();
        }
        back();
    } else if (press == BZ_PRESS_CLICK) {
        return select_item();
    }
    return true;
}

// ---- Drawing --------------------------------------------------------------------

static void draw_list(void) {
    static const char *const titles[] = {"SETUP", "LAYOUT", "", "", "BIG DIGITS", "BRIGHTNESS",
                                         "RESET ALL?"};
    char title[24], label[20], value[16];
    if (m.page == PAGE_METRICS) {
        snprintf(title, sizeof(title), "%s METRICS", bz_page_name(mining_page()));
    } else if (m.page == PAGE_PICK_METRIC) {
        snprintf(title, sizeof(title), "%s: %s", bz_page_name(mining_page()),
                 bz_page_slot_name(mining_page(), m.slot));
    } else {
        snprintf(title, sizeof(title), "%s", titles[m.page]);
    }
    bz_fill(0, 0, LIST_W, 9, BZ_WHITE);
    bz_text(3, 1, title, BZ_FONT_SMALL, BZ_BLACK);

    int count = item_count();
    if (m.cursor < m.top) m.top = m.cursor;
    if (m.cursor >= m.top + ROWS) m.top = m.cursor - ROWS + 1;
    for (int row = 0; row < ROWS && m.top + row < count; ++row) {
        int index = m.top + row, y = LIST_Y + row * ROW_H;
        item_text(index, label, sizeof(label), value, sizeof(value));
        bool selected = index == m.cursor;
        if (selected) bz_fill(0, y, LIST_W - 4, ROW_H, BZ_WHITE);
        int level = selected ? BZ_BLACK : BZ_WHITE;
        bz_text(3, y + 1, label, BZ_FONT_SMALL, level);
        // Long values such as metric names drop to the tiny font to fit.
        int room = LIST_W - 4 - 3 - bz_text_w(label, BZ_FONT_SMALL, 1) - 6;
        if (bz_text_w(value, BZ_FONT_SMALL, 1) <= room)
            bz_text_right(LIST_W - 7, y + 1, value, BZ_FONT_SMALL, selected ? BZ_BLACK : BZ_MID);
        else
            bz_text_right(LIST_W - 7, y + 2, value, BZ_FONT_TINY, selected ? BZ_BLACK : BZ_MID);
    }

    // Scroll track: a dotted rail with a solid thumb.
    if (count > ROWS) {
        int track = ROWS * ROW_H;
        int thumb = track * ROWS / count;
        int offset = (track - thumb) * m.top / (count - ROWS);
        bz_vdots(LIST_W - 2, LIST_Y, track, BZ_MID);
        bz_fill(LIST_W - 3, LIST_Y + offset, 3, thumb, BZ_WHITE);
    }
}

static void draw_preview(void) {
    const int x = 133, w = BZ_W - x;
    bool blink = (bz.now / 300u) % 2u == 0;
    bz_vdots(LIST_W + 2, 0, BZ_H, BZ_MID);

    switch (m.page) {
    case PAGE_DIGITS:
        bz_text(x + 3, 1, "BIG NUMBER STYLE", BZ_FONT_TINY, BZ_MID);
        bz_big_text(x + 3, 12, "1.21", BZ_WHITE);
        bz_text(x + 3 + bz_big_text_w("1.21") + 3, 25, "TH/s", BZ_FONT_LARGE, BZ_WHITE);
        break;
    case PAGE_BRIGHTNESS:
        bz_text(x + 3, 1, "PANEL BRIGHTNESS", BZ_FONT_TINY, BZ_MID);
        for (int i = 0; i < BZ_BRIGHTNESS_LEVELS; ++i) {
            int bx = x + 3 + i * 30, h = 8 + i * 8;
            if (i <= bz.settings.brightness) bz_fill(bx, 46 - h, 26, h, BZ_WHITE);
            else bz_dotted_rect(bx, 46 - h, 26, h, BZ_MID);
        }
        break;
    case PAGE_RESET:
        bz_text(x + 3, 1, "FACTORY SETTINGS", BZ_FONT_TINY, BZ_MID);
        bz_text(x + 3, 14, "RESTORES THE", BZ_FONT_SMALL, BZ_WHITE);
        bz_text(x + 3, 24, "DEFAULT PAGES AND", BZ_FONT_SMALL, BZ_WHITE);
        bz_text(x + 3, 34, "METRICS.", BZ_FONT_SMALL, BZ_WHITE);
        break;
    default: {
        char title[32];
        snprintf(title, sizeof(title), "PAGE %d/%d: %s", bz.settings.page + 1, BZ_PAGE_COUNT,
                 bz_page_name(mining_page()));
        bz_text(x + 3, 1, title, BZ_FONT_TINY, BZ_MID);
        int highlight = m.page == PAGE_PICK_METRIC ? m.slot
                        : m.page == PAGE_METRICS ? m.cursor - 1 : -1;
        bz_mining_draw_preview(x + 2, 10, highlight, m.page != PAGE_PICK_METRIC || blink);
        break;
    }
    }

    const char *hint = is_picker(m.page) ? "PUSH:OK  HOLD:CANCEL"
                       : m.page == PAGE_ROOT ? "TURN:MOVE  PUSH:SELECT" : "PUSH:SELECT  HOLD:BACK";
    bz_hdots(x, 55, w, BZ_MID);
    bz_text(x + 3, 58, hint, BZ_FONT_TINY, BZ_MID);
}

void bz_menu_draw(void) {
    draw_list();
    draw_preview();
}
