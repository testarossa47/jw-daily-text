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
#define KEY_ACTION_SYNC_RANGE MESSAGE_KEY_action_sync_range
#define KEY_START_DATE MESSAGE_KEY_start_date
#define KEY_END_DATE MESSAGE_KEY_end_date

#define PERSIST_KEY_LANGUAGE 1
#define PERSIST_KEY_CACHE_DAYS 2
#define PERSIST_KEY_LIB 3
#define PERSIST_KEY_RSCONF 4
#define PERSIST_KEY_CACHE 5
#define CACHE_SIZE 25
#define PAST_DAYS_KEEP 7
#define FUTURE_DAYS_PREFETCH 14
#define SCROLL_INCREMENT 48
#define ARROW_HEIGHT 19
#define BANNER_HEIGHT 36

typedef struct {
    bool has_data;
    char date[11];
    char ref[128];
    char text[640];
    char commentary[1200];
} DayEntry;

static const int ACTION_FETCH = 1;
static const int ACTION_FETCH_RESULT = 2;
static const int ACTION_FETCH_ERROR = 3;
static const int ACTION_LANGUAGE_CHANGED = 4;
static const int ACTION_SYNC_RANGE = 5;

static Window *s_window;
static ScrollLayer *s_scroll_layer;
static TextLayer *s_body_layer;
static Layer *s_banner_layer;
static char s_banner_text[36];
static Layer *s_bottom_arrow_layer;
static StatusBarLayer *s_status_bar;
static Layer *s_next_bar;
static char s_next_bar_text[36];
static bool s_swap_in_progress;
static int s_screen_height;

static int s_current_day;
static int s_days_in_month;
static int s_current_year;
static int s_current_month;
static bool s_waiting_for_phone;
static char s_language[8];
static char s_lib[16];
static char s_rsconf[8];
static int s_cache_days;

static DayEntry s_cache[CACHE_SIZE];
static bool s_sync_in_progress = false;

static DayEntry *current_entry(void) {
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].has_data && strcmp(s_cache[i].date, date_str) == 0) {
            return &s_cache[i];
        }
    }
    return NULL;
}

static DayEntry *find_cache_entry(const char *date_str) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].has_data && strcmp(s_cache[i].date, date_str) == 0) {
            return &s_cache[i];
        }
    }
    return NULL;
}

static int date_to_days(int year, int month, int day) {
    int days = year * 365 + day;
    days += (year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400;
    int month_days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += month_days[month - 1];
    if (month > 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        days++;
    }
    return days;
}

static void add_days_to_date(int year, int month, int day, int days_to_add, char *result, int result_size) {
    int total_days = date_to_days(year, month, day) + days_to_add;
    int y = 1;
    while (total_days > 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1 : 0)) {
        total_days -= 365 + (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1 : 0);
        y++;
    }
    int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) {
        month_days[1] = 29;
    }
    int m = 1;
    while (total_days > month_days[m - 1]) {
        total_days -= month_days[m - 1];
        m++;
    }
    snprintf(result, result_size, "%d-%02d-%02d", y, m, total_days);
}

static int parse_date(const char *date_str, int *year, int *month, int *day) {
    if (!date_str || strlen(date_str) < 10) return 0;
    
    *year = 0;
    *month = 0;
    *day = 0;
    
    int i = 0;
    while (date_str[i] && date_str[i] != '-') {
        *year = *year * 10 + (date_str[i] - '0');
        i++;
    }
    if (!date_str[i]) return 0;
    i++;
    
    while (date_str[i] && date_str[i] != '-') {
        *month = *month * 10 + (date_str[i] - '0');
        i++;
    }
    if (!date_str[i]) return 0;
    i++;
    
    while (date_str[i]) {
        *day = *day * 10 + (date_str[i] - '0');
        i++;
    }
    
    return (*year > 0 && *month >= 1 && *month <= 12 && *day >= 1 && *day <= 31);
}

static int count_future_cached_days(const char *from_date) {
    int count = 0;
    int year, month, day;
    if (!parse_date(from_date, &year, &month, &day)) return 0;
    
    char next_date[11];
    add_days_to_date(year, month, day, 1, next_date, sizeof(next_date));
    
    while (count < FUTURE_DAYS_PREFETCH) {
        if (!find_cache_entry(next_date)) break;
        count++;
        
        int y, m, d;
        parse_date(next_date, &y, &m, &d);
        add_days_to_date(y, m, d, 1, next_date, sizeof(next_date));
    }
    return count;
}

static void evict_old_entries(void) {
    int current_days = date_to_days(s_current_year, s_current_month, s_current_day);
    int cutoff_days = current_days - PAST_DAYS_KEEP;
    
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_cache[i].has_data) continue;
        
        int year, month, day;
        if (parse_date(s_cache[i].date, &year, &month, &day)) {
            int entry_days = date_to_days(year, month, day);
            if (entry_days < cutoff_days) {
                s_cache[i].has_data = false;
            }
        }
    }
}

static void save_cache_to_persist(void) {
    persist_write_data(PERSIST_KEY_CACHE, s_cache, sizeof(s_cache));
}

static void load_cache_from_persist(void) {
    persist_read_data(PERSIST_KEY_CACHE, s_cache, sizeof(s_cache));
}

static void update_ui(void);
static void request_from_phone(void);
static void load_previous_day(void);
static void reset_scroll_to_top(void);
static void prv_update_indicators(ScrollLayer *scroll_layer, void *context);

static int estimate_text_height(const char *text) {
    int chars_per_line = 15;
    int line_height = 26;
    int lines = 1;
    int col = 0;
    for (int i = 0; text[i]; i++) {
        if (text[i] == '\n') { lines++; col = 0; }
        else { col++; if (col >= chars_per_line) { lines++; col = 0; } }
    }
    return lines * line_height + 4;
}

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
        evict_old_entries();
        s_waiting_for_phone = false;
        update_ui();
        request_from_phone();
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

static void banner_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    graphics_draw_text(ctx, s_banner_text, font, 
                       GRect(8, 0, bounds.size.w - 16, bounds.size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void bottom_arrow_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
    int cx = bounds.size.w / 2;
    int ay = 7;
    graphics_draw_line(ctx, GPoint(cx - 5, ay), GPoint(cx, ay + 6));
    graphics_draw_line(ctx, GPoint(cx + 5, ay), GPoint(cx, ay + 6));
}

static void next_bar_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorBlack);
    GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    graphics_draw_text(ctx, s_next_bar_text, font, bounds,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void prv_update_indicators(ScrollLayer *scroll_layer, void *context) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GSize content = scroll_layer_get_content_size(s_scroll_layer);
    GRect frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    
    bool at_top = (offset.y >= 0);
    bool content_fits = (content.h <= frame.size.h);
    bool has_next = (s_current_day < s_days_in_month);
    bool show_arrow = (at_top && (!content_fits || has_next) && !s_waiting_for_phone);
    layer_set_hidden(s_bottom_arrow_layer, !show_arrow);
}

static void reset_scroll_to_top(void) {
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), true);
}

static void load_previous_day(void) {
    if (s_current_day <= 1) return;
    
    s_current_day--;
    DayEntry *e = current_entry();
    if (e && e->has_data) {
        reset_scroll_to_top();
        update_ui();
    } else {
        s_waiting_for_phone = true;
        reset_scroll_to_top();
        update_ui();
        request_from_phone();
    }
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        if (!s_waiting_for_phone) {
            s_waiting_for_phone = true;
            update_ui();
            request_from_phone();
        }
    }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    
    if (offset.y >= 0) {
        load_previous_day();
    } else {
        int new_y = offset.y + SCROLL_INCREMENT;
        if (new_y > 0) new_y = 0;
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, new_y), true);
    }
}

static void swap_down_complete(Animation *animation, bool finished, void *context) {
    s_swap_in_progress = false;
    if (!finished) return;
    
    s_current_day++;
    if (s_current_day > s_days_in_month) { s_current_day = s_days_in_month; return; }
    
    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        s_waiting_for_phone = true;
    }
    
    GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int scroll_w = scroll_frame.size.w;
    layer_set_frame(text_layer_get_layer(s_body_layer), GRect(0, 0, scroll_w, 8000));
    layer_set_frame(s_next_bar, GRect(0, 0, scroll_w, BANNER_HEIGHT));
    
    update_ui();
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_swap_in_progress) return;
    
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GSize content = scroll_layer_get_content_size(s_scroll_layer);
    GRect frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int max_scroll = -(content.h - frame.size.h);
    
    if (offset.y <= max_scroll) {
        if (s_current_day >= s_days_in_month) return;
        
        int body_h = text_layer_get_content_size(s_body_layer).h + 4;
        s_swap_in_progress = true;
        
        GRect body_from = layer_get_frame(text_layer_get_layer(s_body_layer));
        GRect body_to = body_from;
        body_to.origin.y = -body_h;
        PropertyAnimation *body_anim = property_animation_create_layer_frame(
            text_layer_get_layer(s_body_layer), &body_from, &body_to);
        
        GRect bar_from = layer_get_frame(s_next_bar);
        GRect bar_to = bar_from;
        bar_to.origin.y = 0;
        PropertyAnimation *bar_anim = property_animation_create_layer_frame(
            s_next_bar, &bar_from, &bar_to);
        
        Animation *spawn = animation_spawn_create(
            property_animation_get_animation(body_anim),
            property_animation_get_animation(bar_anim), NULL);
        animation_set_duration(spawn, 200);
        animation_set_curve(spawn, AnimationCurveEaseOut);
        animation_set_handlers(spawn, (AnimationHandlers) {
            .stopped = swap_down_complete,
        }, NULL);
        animation_schedule(spawn);
    } else {
        int new_y = offset.y - SCROLL_INCREMENT;
        if (new_y < max_scroll) new_y = max_scroll;
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, new_y), true);
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

static void update_ui(void) {
    prv_format_date_str(s_banner_text, sizeof(s_banner_text), s_current_year, s_current_month, s_current_day);
    layer_mark_dirty(s_banner_layer);

    if (s_waiting_for_phone) {
        text_layer_set_text(s_body_layer, "Loading...");
        scroll_layer_set_content_size(s_scroll_layer, GSize(192, 200));
        layer_set_hidden(s_bottom_arrow_layer, true);
        layer_set_hidden(s_next_bar, true);
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        text_layer_set_text(s_body_layer, "No data available.\nPress SELECT to retry.");
        scroll_layer_set_content_size(s_scroll_layer, GSize(192, 200));
        layer_set_hidden(s_bottom_arrow_layer, true);
        layer_set_hidden(s_next_bar, true);
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    static char body_text[3500];
    snprintf(body_text, sizeof(body_text), "%s\n\n\"%s\"\n\n%s",
             e->ref, e->text, e->commentary);
    text_layer_set_text(s_body_layer, body_text);

    int measured_h = text_layer_get_content_size(s_body_layer).h + 4;
    int estimated_h = estimate_text_height(body_text);
    int body_h = measured_h > estimated_h ? measured_h : estimated_h;
    layer_mark_dirty(text_layer_get_layer(s_body_layer));
    GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int scroll_w = scroll_frame.size.w;

    if (s_current_day < s_days_in_month) {
        int ny = s_current_year, nm = s_current_month, nd = s_current_day + 1;
        if (nd > days_in_month(ny, nm)) { nd = 1; nm++; if (nm > 12) { nm = 1; ny++; } }
        prv_format_date_str(s_next_bar_text, sizeof(s_next_bar_text), ny, nm, nd);
        layer_set_frame(s_next_bar, GRect(0, body_h, scroll_w, BANNER_HEIGHT));
        layer_set_hidden(s_next_bar, false);
        layer_mark_dirty(s_next_bar);
        scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, body_h + BANNER_HEIGHT));
    } else {
        layer_set_hidden(s_next_bar, true);
        scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, body_h));
    }
    
    prv_update_indicators(s_scroll_layer, NULL);
}

static void request_bulk_sync(const char *start_date) {
    if (s_sync_in_progress) return;
    s_sync_in_progress = true;
    
    char end_date[11];
    int year, month, day;
    if (!parse_date(start_date, &year, &month, &day)) {
        s_sync_in_progress = false;
        return;
    }
    add_days_to_date(year, month, day, FUTURE_DAYS_PREFETCH, end_date, sizeof(end_date));
    
    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
        dict_write_int32(iter, KEY_ACTION, ACTION_SYNC_RANGE);
        dict_write_cstring(iter, KEY_START_DATE, start_date);
        dict_write_cstring(iter, KEY_END_DATE, end_date);
        dict_write_cstring(iter, KEY_LANGUAGE, s_language);
        dict_write_cstring(iter, KEY_LIB, s_lib);
        dict_write_cstring(iter, KEY_RSCONF, s_rsconf);
        app_message_outbox_send();
    } else {
        s_sync_in_progress = false;
    }
}

static void request_from_phone(void) {
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);
    
    DayEntry *cached = find_cache_entry(date_str);
    if (cached) {
        s_waiting_for_phone = false;
        update_ui();
        
        int future_count = count_future_cached_days(date_str);
        if (future_count < FUTURE_DAYS_PREFETCH) {
            request_bulk_sync(date_str);
        }
        return;
    }

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

        const char *date_str = date_t->value->cstring;
        
        DayEntry *existing = find_cache_entry(date_str);
        DayEntry *e = existing;
        
        if (!e) {
            for (int i = 0; i < CACHE_SIZE; i++) {
                if (!s_cache[i].has_data) {
                    e = &s_cache[i];
                    break;
                }
            }
            if (!e) {
                evict_old_entries();
                for (int i = 0; i < CACHE_SIZE; i++) {
                    if (!s_cache[i].has_data) {
                        e = &s_cache[i];
                        break;
                    }
                }
            }
        }
        
        if (!e) return;
        
        strncpy(e->date, date_str, sizeof(e->date) - 1);
        e->date[sizeof(e->date) - 1] = '\0';
        strncpy(e->ref, ref_t->value->cstring, sizeof(e->ref) - 1);
        e->ref[sizeof(e->ref) - 1] = '\0';
        strncpy(e->text, text_t->value->cstring, sizeof(e->text) - 1);
        e->text[sizeof(e->text) - 1] = '\0';
        strncpy(e->commentary, comm_t->value->cstring, sizeof(e->commentary) - 1);
        e->commentary[sizeof(e->commentary) - 1] = '\0';
        e->has_data = true;
        
        save_cache_to_persist();

        int year, month, day;
        if (parse_date(date_str, &year, &month, &day)) {
            if (year == s_current_year && month == s_current_month && day == s_current_day) {
                s_waiting_for_phone = false;
                scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
                update_ui();
            }
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
            if (s_cache_days > CACHE_SIZE) s_cache_days = CACHE_SIZE;
            persist_write_int(PERSIST_KEY_CACHE_DAYS, s_cache_days);
        }
        for (int i = 0; i < CACHE_SIZE; i++) s_cache[i].has_data = false;
        save_cache_to_persist();
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
    s_sync_in_progress = false;
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    s_screen_height = bounds.size.h;

    s_status_bar = status_bar_layer_create();
    status_bar_layer_set_colors(s_status_bar, GColorBlack, GColorWhite);
    status_bar_layer_set_separator_mode(s_status_bar, StatusBarLayerSeparatorModeNone);
    layer_add_child(root, status_bar_layer_get_layer(s_status_bar));

    s_banner_layer = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, bounds.size.w, BANNER_HEIGHT));
    layer_set_update_proc(s_banner_layer, banner_update_proc);
    layer_add_child(root, s_banner_layer);

    s_body_layer = text_layer_create(GRect(0, 0, bounds.size.w - 8, 8000));
    text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
    text_layer_set_text_color(s_body_layer, GColorBlack);
    text_layer_set_background_color(s_body_layer, GColorClear);
    text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);

    s_scroll_layer = scroll_layer_create(GRect(4, STATUS_BAR_LAYER_HEIGHT + BANNER_HEIGHT, bounds.size.w - 8, bounds.size.h - STATUS_BAR_LAYER_HEIGHT - BANNER_HEIGHT));
    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_body_layer));
    
    s_next_bar = layer_create(GRect(0, 0, bounds.size.w - 8, BANNER_HEIGHT));
    layer_set_update_proc(s_next_bar, next_bar_update_proc);
    layer_set_hidden(s_next_bar, true);
    scroll_layer_add_child(s_scroll_layer, s_next_bar);
    
    scroll_layer_set_paging(s_scroll_layer, false);
    scroll_layer_set_shadow_hidden(s_scroll_layer, true);
    scroll_layer_set_callbacks(s_scroll_layer, (ScrollLayerCallbacks) {
        .content_offset_changed_handler = prv_update_indicators
    });
    scroll_layer_set_context(s_scroll_layer, NULL);
    layer_add_child(root, scroll_layer_get_layer(s_scroll_layer));

    s_bottom_arrow_layer = layer_create(GRect(0, bounds.size.h - ARROW_HEIGHT, bounds.size.w, ARROW_HEIGHT));
    layer_set_update_proc(s_bottom_arrow_layer, bottom_arrow_update_proc);
    layer_set_hidden(s_bottom_arrow_layer, true);
    layer_add_child(root, s_bottom_arrow_layer);

    window_set_background_color(window, GColorWhite);
    update_ui();
}

static void window_unload(Window *window) {
    text_layer_destroy(s_body_layer);
    scroll_layer_destroy(s_scroll_layer);
    layer_destroy(s_banner_layer);
    layer_destroy(s_bottom_arrow_layer);
    layer_destroy(s_next_bar);
    status_bar_layer_destroy(s_status_bar);
}

static void init(void) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    s_current_year = local->tm_year + 1900;
    s_current_month = local->tm_mon + 1;
    s_current_day = local->tm_mday;
    s_days_in_month = days_in_month(s_current_year, s_current_month);
    s_waiting_for_phone = false;

    for (int i = 0; i < CACHE_SIZE; i++) s_cache[i].has_data = false;
    load_cache_from_persist();
    evict_old_entries();

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
        if (strncmp(s_language, "de", 2) == 0) {
            strncpy(s_rsconf, "10", sizeof(s_rsconf) - 1);
        } else if (strncmp(s_language, "es", 2) == 0) {
            strncpy(s_rsconf, "4", sizeof(s_rsconf) - 1);
        } else if (strncmp(s_language, "ja", 2) == 0) {
            strncpy(s_rsconf, "7", sizeof(s_rsconf) - 1);
        } else {
            strncpy(s_rsconf, "1", sizeof(s_rsconf) - 1);
        }
        s_rsconf[sizeof(s_rsconf) - 1] = '\0';
    }

    if (persist_exists(PERSIST_KEY_CACHE_DAYS)) {
        s_cache_days = persist_read_int(PERSIST_KEY_CACHE_DAYS);
    } else {
        s_cache_days = 7;
    }
    if (s_cache_days < 1) s_cache_days = 1;
    if (s_cache_days > CACHE_SIZE) s_cache_days = CACHE_SIZE;

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_open(8192, 512);

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
