#include <pebble.h>
#include <message_keys.auto.h>

#define KEY_ACTION MESSAGE_KEY_action
#define KEY_DATE MESSAGE_KEY_date
#define KEY_REF MESSAGE_KEY_ref
#define KEY_TEXT MESSAGE_KEY_text
#define KEY_COMMENTARY MESSAGE_KEY_commentary
#define KEY_ERROR MESSAGE_KEY_error
#define KEY_LANGUAGE MESSAGE_KEY_language
#define KEY_LIB MESSAGE_KEY_lib
#define KEY_RSCONF MESSAGE_KEY_rsconf
#define KEY_CACHE_DAYS MESSAGE_KEY_cache_days
#define KEY_YEAR MESSAGE_KEY_year
#define KEY_MONTH MESSAGE_KEY_month
#define KEY_MONTH_ENTRY_DATE MESSAGE_KEY_month_entry_date
#define KEY_MONTH_ENTRY_REF MESSAGE_KEY_month_entry_ref
#define KEY_MONTH_ENTRY_TEXT MESSAGE_KEY_month_entry_text
#define KEY_MONTH_ENTRY_COMMENTARY MESSAGE_KEY_month_entry_commentary
#define KEY_MONTH_TOTAL MESSAGE_KEY_month_total
#define KEY_MONTH_INDEX MESSAGE_KEY_month_index

#define PERSIST_KEY_LANGUAGE 1
#define PERSIST_KEY_CACHE_DAYS 2
#define PERSIST_KEY_LIB 3
#define PERSIST_KEY_RSCONF 4
#define MAX_CACHE_DAYS 14

typedef struct {
    bool has_data;
    char ref[128];
    char text[512];
    char commentary[512];
} DayEntry;

static const int ACTION_FETCH = 1;
static const int ACTION_FETCH_RESULT = 2;
static const int ACTION_FETCH_ERROR = 3;
static const int ACTION_LANGUAGE_CHANGED = 4;

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
static char s_language[8];
static char s_lib[16];
static char s_rsconf[8];
static int s_cache_days;

static DayEntry s_cache[MAX_CACHE_DAYS];

static DayEntry *current_entry(void) {
    if (s_current_day >= 1 && s_current_day <= s_days_in_month) {
        return &s_cache[s_current_day - 1];
    }
    return NULL;
}

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
        for (int i = 0; i < MAX_CACHE_DAYS; i++) s_cache[i].has_data = false;
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

static void line_layer_update(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w, 0));
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        if (!s_waiting_for_phone) {
            s_waiting_for_phone = true;
            update_ui();
            request_from_phone();
        }
        return;
    }
    s_mode = (s_mode == MODE_VERSE) ? MODE_COMMENTARY : MODE_VERSE;
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
    update_ui();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    if (offset.y < 0) {
        GRect frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
        int new_y = offset.y + frame.size.h;
        if (new_y > 0) new_y = 0;
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, new_y), true);
    } else if (s_current_day > 1) {
        s_current_day--;
        s_mode = MODE_VERSE;
        DayEntry *e = current_entry();
        if (e && e->has_data) {
            scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
            update_ui();
        } else {
            s_waiting_for_phone = true;
            update_ui();
            request_from_phone();
        }
    }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GSize content = scroll_layer_get_content_size(s_scroll_layer);
    GRect frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int max_scroll = content.h - frame.size.h;
    if (max_scroll > 0 && offset.y > -max_scroll) {
        int new_y = offset.y - frame.size.h;
        if (new_y < -max_scroll) new_y = -max_scroll;
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, new_y), true);
    } else if (s_current_day < s_days_in_month) {
        s_current_day++;
        s_mode = MODE_VERSE;
        DayEntry *e = current_entry();
        if (e && e->has_data) {
            scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
            update_ui();
        } else {
            s_waiting_for_phone = true;
            update_ui();
            request_from_phone();
        }
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
    update_date_display();

    if (s_waiting_for_phone) {
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, "Loading...");
        scroll_layer_set_content_size(s_scroll_layer, GSize(192, 200));
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, "No data available.\nPress SELECT to retry.");
        scroll_layer_set_content_size(s_scroll_layer, GSize(192, 200));
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    text_layer_set_text(s_ref_layer, e->ref);

    static char body_text[514];
    if (s_mode == MODE_VERSE) {
        snprintf(body_text, sizeof(body_text), "\"%s\"", e->text);
    } else {
        snprintf(body_text, sizeof(body_text), "%s", e->commentary);
    }
    text_layer_set_text(s_body_layer, body_text);

    GSize max_size = text_layer_get_content_size(s_body_layer);
    int content_h = max_size.h + 4;
    scroll_layer_set_content_size(s_scroll_layer, GSize(192, content_h));
    layer_mark_dirty(text_layer_get_layer(s_body_layer));
}

static void request_from_phone(void) {
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);

    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_int32(iter, KEY_ACTION, ACTION_FETCH);
    dict_write_cstring(iter, KEY_DATE, date_str);
    dict_write_cstring(iter, KEY_LANGUAGE, s_language);
    dict_write_cstring(iter, KEY_LIB, s_lib);
    dict_write_cstring(iter, KEY_RSCONF, s_rsconf);
    dict_write_int32(iter, KEY_CACHE_DAYS, s_cache_days);
    app_message_outbox_send();
}

static int parse_day_from_date(const char *s) {
    int year = 0, month = 0, day = 0;
    int part = 0;
    for (; *s; s++) {
        if (*s == '-') { part++; continue; }
        if (*s < '0' || *s > '9') return -1;
        int d = *s - '0';
        if (part == 0) year = year * 10 + d;
        else if (part == 1) month = month * 10 + d;
        else if (part == 2) day = day * 10 + d;
    }
    return (part == 2 && day >= 1) ? day : -1;
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
        if (!date_t || !ref_t || !text_t || !comm_t) return;

        int day = parse_day_from_date(date_t->value->cstring);
        if (day < 1 || day > s_days_in_month) return;

        DayEntry *e = &s_cache[day - 1];
        strncpy(e->ref, ref_t->value->cstring, sizeof(e->ref) - 1);
        e->ref[sizeof(e->ref) - 1] = '\0';
        strncpy(e->text, text_t->value->cstring, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
        strncpy(e->commentary, comm_t->value->cstring, sizeof(e->commentary) - 1);
        e->commentary[sizeof(e->commentary) - 1] = '\0';
        e->has_data = true;

        if (day == s_current_day) {
            s_waiting_for_phone = false;
            scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
            update_ui();
        }
    } else if (action == ACTION_FETCH_ERROR) {
        s_waiting_for_phone = false;
        Tuple *err_t = dict_find(iter, KEY_ERROR);
        static char err_msg[128];
        if (err_t) {
            snprintf(err_msg, sizeof(err_msg), "Fetch error: %s\nSELECT to retry.", err_t->value->cstring);
        } else {
            snprintf(err_msg, sizeof(err_msg), "Failed to load.\nPress SELECT to retry.");
        }
        text_layer_set_text(s_ref_layer, "");
        text_layer_set_text(s_body_layer, err_msg);
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
    } else if (action == ACTION_LANGUAGE_CHANGED) {
        Tuple *lang_t = dict_find(iter, KEY_LANGUAGE);
        Tuple *lib_t = dict_find(iter, KEY_LIB);
        Tuple *rsconf_t = dict_find(iter, KEY_RSCONF);
        Tuple *cd_t = dict_find(iter, KEY_CACHE_DAYS);
        if (lang_t) {
            strncpy(s_language, lang_t->value->cstring, sizeof(s_language) - 1);
            s_language[sizeof(s_language) - 1] = '\0';
            persist_write_string(PERSIST_KEY_LANGUAGE, s_language);
        }
        if (lib_t) {
            strncpy(s_lib, lib_t->value->cstring, sizeof(s_lib) - 1);
            s_lib[sizeof(s_lib) - 1] = '\0';
            persist_write_string(PERSIST_KEY_LIB, s_lib);
        }
        if (rsconf_t) {
            strncpy(s_rsconf, rsconf_t->value->cstring, sizeof(s_rsconf) - 1);
            s_rsconf[sizeof(s_rsconf) - 1] = '\0';
            persist_write_string(PERSIST_KEY_RSCONF, s_rsconf);
        }
        if (cd_t) {
            s_cache_days = cd_t->value->int32;
            if (s_cache_days < 1) s_cache_days = 1;
            if (s_cache_days > MAX_CACHE_DAYS) s_cache_days = MAX_CACHE_DAYS;
            persist_write_int(PERSIST_KEY_CACHE_DAYS, s_cache_days);
        }
        for (int i = 0; i < MAX_CACHE_DAYS; i++) s_cache[i].has_data = false;
        s_waiting_for_phone = true;
        update_ui();
        request_from_phone();
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

    s_date_layer = text_layer_create(GRect(4, 4, bounds.size.w - 8, 34));
    text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    text_layer_set_text_color(s_date_layer, GColorBlack);
    text_layer_set_background_color(s_date_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_date_layer));

    s_line_layer = layer_create(GRect(4, 40, bounds.size.w - 8, 2));
    layer_set_update_proc(s_line_layer, line_layer_update);
    layer_add_child(root, s_line_layer);

    s_ref_layer = text_layer_create(GRect(4, 44, bounds.size.w - 8, 30));
    text_layer_set_font(s_ref_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    text_layer_set_text_alignment(s_ref_layer, GTextAlignmentCenter);
    text_layer_set_text_color(s_ref_layer, GColorBlack);
    text_layer_set_background_color(s_ref_layer, GColorClear);
    layer_add_child(root, text_layer_get_layer(s_ref_layer));

    s_body_layer = text_layer_create(GRect(0, 0, bounds.size.w - 8, 3000));
    text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
    text_layer_set_text_color(s_body_layer, GColorBlack);
    text_layer_set_background_color(s_body_layer, GColorClear);
    text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);

    s_scroll_layer = scroll_layer_create(GRect(4, 78, bounds.size.w - 8, bounds.size.h - 78));
    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_body_layer));
    scroll_layer_set_paging(s_scroll_layer, false);
    layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

    window_set_background_color(window, GColorWhite);
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

    for (int i = 0; i < MAX_CACHE_DAYS; i++) s_cache[i].has_data = false;

    if (persist_exists(PERSIST_KEY_LANGUAGE)) {
        persist_read_string(PERSIST_KEY_LANGUAGE, s_language, sizeof(s_language));
    } else {
        strncpy(s_language, "en", sizeof(s_language) - 1);
        s_language[sizeof(s_language) - 1] = '\0';
    }

    if (persist_exists(PERSIST_KEY_LIB)) {
        persist_read_string(PERSIST_KEY_LIB, s_lib, sizeof(s_lib));
    } else {
        strncpy(s_lib, "lp-e", sizeof(s_lib) - 1);
        s_lib[sizeof(s_lib) - 1] = '\0';
    }

    if (persist_exists(PERSIST_KEY_RSCONF)) {
        persist_read_string(PERSIST_KEY_RSCONF, s_rsconf, sizeof(s_rsconf));
    } else {
        strncpy(s_rsconf, "1", sizeof(s_rsconf) - 1);
        s_rsconf[sizeof(s_rsconf) - 1] = '\0';
    }

    if (persist_exists(PERSIST_KEY_CACHE_DAYS)) {
        s_cache_days = persist_read_int(PERSIST_KEY_CACHE_DAYS);
    } else {
        s_cache_days = 7;
    }
    if (s_cache_days < 1) s_cache_days = 1;
    if (s_cache_days > MAX_CACHE_DAYS) s_cache_days = MAX_CACHE_DAYS;

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_open(2048, 512);

    s_window = window_create();
    window_set_window_handlers(s_window, (WindowHandlers){
        .load = window_load,
        .unload = window_unload,
    });
    window_set_click_config_provider(s_window, click_config_provider);
    window_stack_push(s_window, true);

    request_from_phone();
}

static void deinit(void) {
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
