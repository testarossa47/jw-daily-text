#include <pebble.h>
#include "jw_texts_data.h"

#define KEY_ACTION 0
#define KEY_DATE 1
#define KEY_REF 2
#define KEY_TEXT 3
#define KEY_COMMENTARY 4
#define KEY_ERROR 5
#define KEY_YEAR 6
#define KEY_MONTH 7
#define KEY_MONTH_ENTRY_DATE 8
#define KEY_MONTH_ENTRY_REF 9
#define KEY_MONTH_ENTRY_TEXT 10
#define KEY_MONTH_ENTRY_COMMENTARY 11
#define KEY_MONTH_TOTAL 12
#define KEY_MONTH_INDEX 13

static const int ACTION_FETCH = 1;
static const int ACTION_FETCH_RESULT = 2;
static const int ACTION_FETCH_ERROR = 3;

typedef enum { MODE_VERSE, MODE_COMMENTARY } DisplayMode;

static Window *s_window;
static TextLayer *s_date_layer;
static TextLayer *s_ref_layer;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_body_layer;
static Layer *s_line_layer;

static int s_current_day;
static int s_days_in_month;
static int s_current_year;
static int s_current_month;
static DisplayMode s_mode;
static bool s_waiting_for_phone;

static DailyTextEntry s_remote_entry;
static bool s_has_remote_entry;
static char s_remote_date[11];

static void update_ui(void);
static void request_from_phone(void);

static int days_in_month(int year, int month) {
    if (month == 2) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        return leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11)
        return 30;
    return 31;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    if (s_current_year != (tick_time->tm_year + 1900) ||
        s_current_month != (tick_time->tm_mon + 1) ||
        s_current_day != tick_time->tm_mday) {
        s_current_year = tick_time->tm_year + 1900;
        s_current_month = tick_time->tm_mon + 1;
        s_current_day = tick_time->tm_mday;
        s_days_in_month = days_in_month(s_current_year, s_current_month);
        s_has_remote_entry = false;
        s_waiting_for_phone = false;
        s_mode = MODE_VERSE;
        update_ui();
    }
}

static void prv_format_date_str(char *buf, int size, int y, int m, int d) {
    static const char *names[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    const char *mon = (m >= 1 && m <= 12) ? names[m - 1] : "?";
    const char *sfx = "th";
    if (d >= 11 && d <= 13) sfx = "th";
    else if (d % 10 == 1) sfx = "st";
    else if (d % 10 == 2) sfx = "nd";
    else if (d % 10 == 3) sfx = "rd";
    snprintf(buf, size, "%s %d%s, %d", mon, d, sfx, y);
}

static bool is_local_month(int year, int month) {
    return year == 2026 && month == 7;
}

static void line_layer_update(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_stroke_color(ctx, GColorLightGray);
    graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    s_mode = (s_mode == MODE_VERSE) ? MODE_COMMENTARY : MODE_VERSE;
    update_ui();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_current_day > 1) {
        s_current_day--;
        s_has_remote_entry = false;
        s_waiting_for_phone = false;
        s_mode = MODE_VERSE;
        update_ui();
    }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_current_day < s_days_in_month) {
        s_current_day++;
        s_has_remote_entry = false;
        s_waiting_for_phone = false;
        s_mode = MODE_VERSE;
        update_ui();
    }
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop(true);
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void update_date_display(void) {
    static char date_buf[36];
    prv_format_date_str(date_buf, sizeof(date_buf), s_current_year, s_current_month, s_current_day);
    text_layer_set_text(s_date_layer, date_buf);
}

static void update_ui(void) {
    DailyTextEntry entry;
    bool found = false;

    if (s_has_remote_entry) {
        entry = s_remote_entry;
        found = true;
    } else if (is_local_month(s_current_year, s_current_month)) {
        int idx = s_current_day - 1;
        if (idx >= 0 && idx < DAILY_TEXT_COUNT) {
            find_entry_by_index(idx, &entry);
            found = true;
        }
    }

    if (!found && !s_waiting_for_phone) {
        s_waiting_for_phone = true;
        request_from_phone();
    }

    update_date_display();

    if (s_waiting_for_phone) {
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, "Loading...");
        scroll_layer_set_content_size(s_scroll_layer, GSize(144, 2000));
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    if (!found) {
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, "No data available.");
        scroll_layer_set_content_size(s_scroll_layer, GSize(144, 2000));
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    text_layer_set_text(s_ref_layer, entry.ref);

    static char body_text[2000];
    if (s_mode == MODE_VERSE) {
        snprintf(body_text, sizeof(body_text), "\"%s\"", entry.text);
    } else {
        snprintf(body_text, sizeof(body_text), "%s", entry.commentary);
    }
    text_layer_set_text(s_body_layer, body_text);

    GSize max_size = text_layer_get_content_size(s_body_layer);
    scroll_layer_set_content_size(s_scroll_layer, GSize(144, max_size.h + 4));
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
    layer_mark_dirty(text_layer_get_layer(s_body_layer));
}

static void request_from_phone(void) {
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);

    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_int32(iter, KEY_ACTION, ACTION_FETCH);
    dict_write_cstring(iter, KEY_DATE, date_str);
    app_message_outbox_send();
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *action_t = dict_find(iter, KEY_ACTION);
    if (!action_t) return;

    int action = action_t->value->int32;

    if (action == ACTION_FETCH_RESULT) {
        Tuple *date_t = dict_find(iter, KEY_DATE);
        Tuple *ref_t = dict_find(iter, KEY_REF);
        Tuple *text_t = dict_find(iter, KEY_TEXT);
        Tuple *comm_t = dict_find(iter, KEY_COMMENTARY);
        if (date_t && ref_t && text_t && comm_t) {
            strncpy(s_remote_date, date_t->value->cstring, sizeof(s_remote_date) - 1);
            s_remote_date[sizeof(s_remote_date) - 1] = 0;
            s_remote_entry.date = s_remote_date;
            s_remote_entry.ref = ref_t->value->cstring;
            s_remote_entry.text = text_t->value->cstring;
            s_remote_entry.commentary = comm_t->value->cstring;
            s_has_remote_entry = true;
            s_waiting_for_phone = false;
            update_ui();
        }
    } else if (action == ACTION_FETCH_ERROR) {
        s_waiting_for_phone = false;
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, "Failed to load.\nPress any key.");
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
    }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", reason);
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", reason);
    s_waiting_for_phone = false;
    text_layer_set_text(s_body_layer, "Phone not connected.\nPress BACK to exit.");
    layer_mark_dirty(text_layer_get_layer(s_body_layer));
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_date_layer = text_layer_create(GRect(4, 0, bounds.size.w - 8, 24));
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_background_color(s_date_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    s_line_layer = layer_create(GRect(4, 24, bounds.size.w - 8, 1));
    layer_set_update_proc(s_line_layer, line_layer_update);
    layer_add_child(root, s_line_layer);

    s_ref_layer = text_layer_create(GRect(4, 28, bounds.size.w - 8, 22));
    text_layer_set_font(s_ref_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    text_layer_set_text_alignment(s_ref_layer, GTextAlignmentCenter);
    text_layer_set_text_color(s_ref_layer, GColorWhite);
    text_layer_set_background_color(s_ref_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_ref_layer));

    s_body_layer = text_layer_create(GRect(0, 0, bounds.size.w - 8, 2000));
    text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_text_color(s_body_layer, GColorWhite);
    text_layer_set_background_color(s_body_layer, GColorClear);
    text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);

    s_scroll_layer = scroll_layer_create(GRect(4, 52, bounds.size.w - 8, bounds.size.h - 52));
    scroll_layer_set_click_config_onto_window(s_scroll_layer, window);
    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_body_layer));
    scroll_layer_set_paging(s_scroll_layer, false);
    layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

    window_set_background_color(window, GColorBlack);
    update_ui();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_date_layer);
    text_layer_destroy(s_ref_layer);
    text_layer_destroy(s_body_layer);
    scroll_layer_destroy(s_scroll_layer);
    layer_destroy(s_line_layer);
}

static void init(void) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    s_current_year = local->tm_year + 1900;
    s_current_month = local->tm_mon + 1;
    s_current_day = local->tm_mday;
    s_days_in_month = days_in_month(s_current_year, s_current_month);
    s_mode = MODE_VERSE;
    s_waiting_for_phone = false;
    s_has_remote_entry = false;

    if (is_local_month(s_current_year, s_current_month)) {
        if (s_current_day < 1 || s_current_day > DAILY_TEXT_COUNT)
            s_current_day = 1;
    }

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_open(2048, 256);

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load,
        .unload = window_unload,
    });
    window_set_click_config_provider(s_window, click_config_provider);
    window_stack_push(s_window, true);
}

static void deinit(void) {
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
