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
#define SCROLL_INCREMENT 36
#define SCROLL_REPEAT_INTERVAL_MS 220
#define ARROW_HEIGHT 19
#define BANNER_HEIGHT 36
#define SWAP_ANIM_DURATION_MS 220
#define REVEAL_ANIM_DURATION_MS 260
#define LOADING_FILL_DURATION_MS 2400
#define LOADING_DONE_DURATION_MS 180
#define TOUCH_SWAP_THRESHOLD 30

#define COLOR_BANNER PBL_IF_COLOR_ELSE(GColorVividViolet, GColorDarkGray)
#define COLOR_BANNER_TEXT GColorWhite
#define COLOR_NEXT_BAR PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorLightGray)
#define COLOR_NEXT_BAR_TEXT GColorBlack

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
static Layer *s_temp_bar;
static char s_temp_bar_text[36];
static Layer *s_loading_layer;
static bool s_loading_active;
static int s_loading_progress;
static Animation *s_loading_animation;
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
static Animation *s_scroll_animation = NULL;
static int s_scroll_anim_start_y = 0;
static int s_scroll_anim_target_y = 0;
static AnimationImplementation s_scroll_anim_impl;

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

/* 0 = Monday ... 6 = Sunday. Anchor: 2026-07-17 was a Friday (index 4). */
static int weekday_of(int year, int month, int day) {
    return (date_to_days(year, month, day) + 5) % 7;
}

static bool is_today(int year, int month, int day) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    return year == local->tm_year + 1900 &&
           month == local->tm_mon + 1 &&
           day == local->tm_mday;
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
static void swap_down_complete(Animation *animation, bool finished, void *context);
static void swap_up_complete(Animation *animation, bool finished, void *context);
static void loading_start(void);
static void loading_cancel(void);

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
    static const char *dow[] = {
        "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
    };
    const char *mon = (m >= 1 && m <= 12) ? names[m - 1] : "?";
    const char *day_name = dow[weekday_of(y, m, d)];
    const char *sfx = "th";
    if (d >= 11 && d <= 13) sfx = "th";
    else if (d % 10 == 1) sfx = "st";
    else if (d % 10 == 2) sfx = "nd";
    else if (d % 10 == 3) sfx = "rd";
    snprintf(buf, size, "%s %s %d%s", day_name, mon, d, sfx);
}

static void draw_bar(GContext *ctx, GRect bounds, const char *text,
                     GColor bg, GColor fg, GFont font) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);
    graphics_context_set_text_color(ctx, fg);
    graphics_draw_text(ctx, text, font,
                       GRect(8, 0, bounds.size.w - 16, bounds.size.h),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void banner_update_proc(Layer *layer, GContext *ctx) {
    draw_bar(ctx, layer_get_bounds(layer), s_banner_text,
             COLOR_BANNER, COLOR_BANNER_TEXT,
             fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
}

static void temp_bar_update_proc(Layer *layer, GContext *ctx) {
    draw_bar(ctx, layer_get_bounds(layer), s_temp_bar_text,
             COLOR_BANNER, COLOR_BANNER_TEXT,
             fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
}

static void loading_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int bar_w = bounds.size.w - 48;
    int x = 24;
    int y = bounds.size.h / 2 - 3;
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_rect(ctx, GRect(x, y, bar_w, 6), 3, GCornersAll);
    int fill = bar_w * s_loading_progress / 100;
    if (fill > 0) {
        graphics_context_set_fill_color(ctx, COLOR_BANNER);
        graphics_fill_rect(ctx, GRect(x, y, fill, 6), fill < 6 ? fill / 2 : 3, GCornersAll);
    }
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
    draw_bar(ctx, layer_get_bounds(layer), s_next_bar_text,
             COLOR_NEXT_BAR, COLOR_NEXT_BAR_TEXT,
             fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
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

static void reveal_body(void) {
    GRect to = layer_get_frame(text_layer_get_layer(s_body_layer));
    GRect from = to;
    from.origin.y += 24;
    PropertyAnimation *pa = property_animation_create_layer_frame(
        text_layer_get_layer(s_body_layer), &from, &to);
    Animation *a = property_animation_get_animation(pa);
    animation_set_duration(a, REVEAL_ANIM_DURATION_MS);
    animation_set_curve(a, AnimationCurveEaseOut);
    animation_schedule(a);
}

static int s_loading_anim_start;
static int s_loading_anim_target;
static AnimationImplementation s_loading_anim_impl;

static void loading_anim_update(Animation *animation, const AnimationProgress progress) {
    s_loading_progress = s_loading_anim_start +
        ((s_loading_anim_target - s_loading_anim_start) * (int)progress) / ANIMATION_NORMALIZED_MAX;
    layer_mark_dirty(s_loading_layer);
}

static void loading_anim_stopped(Animation *animation, bool finished, void *context) {
    s_loading_animation = NULL;
    if (finished && s_loading_anim_target >= 100 && s_loading_active) {
        s_loading_active = false;
        layer_set_hidden(s_loading_layer, true);
        reveal_body();
    }
}

static void loading_animate_to(int target, uint32_t duration) {
    if (s_loading_animation) {
        animation_unschedule(s_loading_animation);
        s_loading_animation = NULL;
    }
    s_loading_anim_start = s_loading_progress;
    s_loading_anim_target = target;
    s_loading_anim_impl.update = loading_anim_update;
    s_loading_animation = animation_create();
    animation_set_implementation(s_loading_animation, &s_loading_anim_impl);
    animation_set_duration(s_loading_animation, duration);
    animation_set_curve(s_loading_animation, AnimationCurveEaseOut);
    animation_set_handlers(s_loading_animation, (AnimationHandlers) {
        .stopped = loading_anim_stopped
    }, NULL);
    animation_schedule(s_loading_animation);
}

static void loading_start(void) {
    if (s_loading_active) return;
    s_loading_active = true;
    s_loading_progress = 0;
    layer_set_hidden(s_loading_layer, false);
    layer_mark_dirty(s_loading_layer);
    loading_animate_to(92, LOADING_FILL_DURATION_MS);
}

static void loading_cancel(void) {
    if (s_loading_animation) {
        animation_unschedule(s_loading_animation);
        s_loading_animation = NULL;
    }
    s_loading_active = false;
    layer_set_hidden(s_loading_layer, true);
}

static void loading_finish(void) {
    if (!s_loading_active) return;
    loading_animate_to(100, LOADING_DONE_DURATION_MS);
}

static void load_previous_day(void) {
    if (s_current_day <= 1) return;

    char prev_date[11];
    add_days_to_date(s_current_year, s_current_month, s_current_day, -1, prev_date, sizeof(prev_date));
    DayEntry *e = find_cache_entry(prev_date);

    if (!e) {
        s_current_day--;
        s_waiting_for_phone = true;
        reset_scroll_to_top();
        update_ui();
        request_from_phone();
        return;
    }

    /* Animated swap: old date bar slides down off screen, new content slides in from top */
    s_swap_in_progress = true;
    if (s_scroll_animation) {
        animation_unschedule(s_scroll_animation);
        s_scroll_animation = NULL;
    }

    Layer *root = window_get_root_layer(s_window);
    GRect root_bounds = layer_get_bounds(root);

    snprintf(s_temp_bar_text, sizeof(s_temp_bar_text), "%s", s_banner_text);
    s_temp_bar = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT, root_bounds.size.w, BANNER_HEIGHT));
    layer_set_update_proc(s_temp_bar, temp_bar_update_proc);
    layer_add_child(root, s_temp_bar);

    s_current_day--;
    update_ui();
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);

    int body_h = layer_get_frame(text_layer_get_layer(s_body_layer)).size.h;
    GRect body_to = layer_get_frame(text_layer_get_layer(s_body_layer));
    GRect body_from = body_to;
    body_from.origin.y = -body_h;
    PropertyAnimation *body_anim = property_animation_create_layer_frame(
        text_layer_get_layer(s_body_layer), &body_from, &body_to);

    GRect bar_from = layer_get_frame(s_temp_bar);
    GRect bar_to = bar_from;
    bar_to.origin.y = root_bounds.size.h;
    PropertyAnimation *bar_anim = property_animation_create_layer_frame(
        s_temp_bar, &bar_from, &bar_to);

    Animation *spawn = animation_spawn_create(
        property_animation_get_animation(body_anim),
        property_animation_get_animation(bar_anim), NULL);
    animation_set_duration(spawn, SWAP_ANIM_DURATION_MS);
    animation_set_curve(spawn, AnimationCurveEaseOut);
    animation_set_handlers(spawn, (AnimationHandlers) {
        .stopped = swap_up_complete,
    }, NULL);
    animation_schedule(spawn);
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

static void scroll_anim_update(Animation *animation, const AnimationProgress progress) {
    int current_y = s_scroll_anim_start_y +
        ((s_scroll_anim_target_y - s_scroll_anim_start_y) * (int)progress) / ANIMATION_NORMALIZED_MAX;
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, current_y), false);
}

static void scroll_anim_stopped(Animation *animation, bool finished, void *context) {
    s_scroll_animation = NULL;
}

static void animate_scroll_to(int target_y) {
    if (s_scroll_animation) {
        animation_unschedule(s_scroll_animation);
        s_scroll_animation = NULL;
    }
    GPoint current = scroll_layer_get_content_offset(s_scroll_layer);
    s_scroll_anim_start_y = current.y;
    s_scroll_anim_target_y = target_y;

    s_scroll_anim_impl.update = scroll_anim_update;
    s_scroll_animation = animation_create();
    animation_set_implementation(s_scroll_animation, &s_scroll_anim_impl);
    animation_set_duration(s_scroll_animation, SCROLL_REPEAT_INTERVAL_MS);
    animation_set_curve(s_scroll_animation, AnimationCurveLinear);
    animation_set_handlers(s_scroll_animation, (AnimationHandlers) {
        .stopped = scroll_anim_stopped
    }, NULL);
    animation_schedule(s_scroll_animation);
}

static void scroll_up_one_step(void) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    
    if (offset.y >= 0) {
        load_previous_day();
    } else {
        int new_y = offset.y + SCROLL_INCREMENT;
        if (new_y > 0) new_y = 0;
        animate_scroll_to(new_y);
    }
}

static void scroll_down_one_step(void) {
    GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
    GSize content = scroll_layer_get_content_size(s_scroll_layer);
    GRect frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int max_scroll = -(content.h - frame.size.h);
    
    if (offset.y <= max_scroll) {
        if (s_current_day >= s_days_in_month) return;

        int body_h = layer_get_frame(text_layer_get_layer(s_body_layer)).size.h;
        s_swap_in_progress = true;

        if (s_scroll_animation) {
            animation_unschedule(s_scroll_animation);
            s_scroll_animation = NULL;
        }

        /* Reparent the next-day bar onto the root layer so it can slide up
           over the scroll viewport and become the header bar. */
        GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
        GRect bar_frame = layer_get_frame(s_next_bar);
        int screen_y = scroll_frame.origin.y + bar_frame.origin.y + offset.y;

        Layer *root = window_get_root_layer(s_window);
        layer_remove_from_parent(s_next_bar);
        layer_add_child(root, s_next_bar);
        layer_set_frame(s_next_bar, GRect(0, screen_y, scroll_frame.size.w, BANNER_HEIGHT));

        GRect body_from = layer_get_frame(text_layer_get_layer(s_body_layer));
        GRect body_to = body_from;
        body_to.origin.y = -body_h;
        PropertyAnimation *body_anim = property_animation_create_layer_frame(
            text_layer_get_layer(s_body_layer), &body_from, &body_to);

        GRect bar_from = layer_get_frame(s_next_bar);
        GRect bar_to = GRect(0, STATUS_BAR_LAYER_HEIGHT, scroll_frame.size.w, BANNER_HEIGHT);
        PropertyAnimation *bar_anim = property_animation_create_layer_frame(
            s_next_bar, &bar_from, &bar_to);

        Animation *spawn = animation_spawn_create(
            property_animation_get_animation(body_anim),
            property_animation_get_animation(bar_anim), NULL);
        animation_set_duration(spawn, SWAP_ANIM_DURATION_MS);
        animation_set_curve(spawn, AnimationCurveEaseOut);
        animation_set_handlers(spawn, (AnimationHandlers) {
            .stopped = swap_down_complete,
        }, NULL);
        animation_schedule(spawn);
    } else {
        int new_y = offset.y - SCROLL_INCREMENT;
        if (new_y < max_scroll) new_y = max_scroll;
        animate_scroll_to(new_y);
    }
}

static void swap_down_complete(Animation *animation, bool finished, void *context) {
    s_swap_in_progress = false;

    if (finished) {
        s_current_day++;
        if (s_current_day > s_days_in_month) s_current_day = s_days_in_month;

        DayEntry *e = current_entry();
        if (!e || !e->has_data) {
            s_waiting_for_phone = true;
        }
    }

    layer_remove_from_parent(s_next_bar);
    scroll_layer_add_child(s_scroll_layer, s_next_bar);

    update_ui();
    scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);

    if (s_waiting_for_phone) {
        request_from_phone();
    }
}

static void swap_up_complete(Animation *animation, bool finished, void *context) {
    s_swap_in_progress = false;
    if (s_temp_bar) {
        layer_remove_from_parent(s_temp_bar);
        layer_destroy(s_temp_bar);
        s_temp_bar = NULL;
    }
    if (!finished) {
        update_ui();
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
    }
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_swap_in_progress) return;
    
    if (click_recognizer_is_repeating(recognizer)) {
        GPoint offset = scroll_layer_get_content_offset(s_scroll_layer);
        if (offset.y >= 0) return;
    }
    
    scroll_up_one_step();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (s_swap_in_progress) return;
    scroll_down_one_step();
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_pop(true);
}

static bool s_touch_active;
static int16_t s_touch_start_y;
static int s_touch_start_offset;
static int s_touch_raw_offset;

static void touch_handler(const TouchEvent *event, void *context) {
    if (s_swap_in_progress) return;

    GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    GSize content = scroll_layer_get_content_size(s_scroll_layer);
    int max_scroll = -(content.h - scroll_frame.size.h);
    if (max_scroll > 0) max_scroll = 0;

    if (event->type == TouchEvent_Touchdown) {
        if (event->y < scroll_frame.origin.y ||
            event->y > scroll_frame.origin.y + scroll_frame.size.h) {
            return;
        }
        if (s_scroll_animation) {
            animation_unschedule(s_scroll_animation);
            s_scroll_animation = NULL;
        }
        s_touch_active = true;
        s_touch_start_y = event->y;
        s_touch_start_offset = scroll_layer_get_content_offset(s_scroll_layer).y;
        s_touch_raw_offset = s_touch_start_offset;
    } else if (event->type == TouchEvent_PositionUpdate) {
        if (!s_touch_active) return;
        int raw = s_touch_start_offset + ((int)s_touch_start_y - event->y);
        s_touch_raw_offset = raw;
        int clamped = raw;
        if (clamped > 0) clamped = 0;
        if (clamped < max_scroll) clamped = max_scroll;
        scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, clamped), false);
    } else if (event->type == TouchEvent_Liftoff) {
        if (!s_touch_active) return;
        s_touch_active = false;
        if (s_touch_raw_offset < max_scroll - TOUCH_SWAP_THRESHOLD) {
            scroll_down_one_step();
        } else if (s_touch_raw_offset > TOUCH_SWAP_THRESHOLD) {
            scroll_up_one_step();
        }
    }
}

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_UP, SCROLL_REPEAT_INTERVAL_MS, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, SCROLL_REPEAT_INTERVAL_MS, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void update_ui(void) {
    if (is_today(s_current_year, s_current_month, s_current_day)) {
        snprintf(s_banner_text, sizeof(s_banner_text), "Today");
    } else {
        prv_format_date_str(s_banner_text, sizeof(s_banner_text), s_current_year, s_current_month, s_current_day);
    }
    layer_mark_dirty(s_banner_layer);

    GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
    int scroll_w = scroll_frame.size.w;

    if (s_waiting_for_phone) {
        layer_set_hidden(text_layer_get_layer(s_body_layer), true);
        scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, scroll_frame.size.h));
        layer_set_hidden(s_bottom_arrow_layer, true);
        layer_set_hidden(s_next_bar, true);
        loading_start();
        return;
    }

    DayEntry *e = current_entry();
    if (!e || !e->has_data) {
        loading_cancel();
        text_layer_set_text(s_body_layer, "No data available.\nPress SELECT to retry.");
        layer_set_frame(text_layer_get_layer(s_body_layer), GRect(4, 0, scroll_w - 8, 200));
        layer_set_hidden(text_layer_get_layer(s_body_layer), false);
        scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, scroll_frame.size.h));
        layer_set_hidden(s_bottom_arrow_layer, true);
        layer_set_hidden(s_next_bar, true);
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    static char body_text[3500];
    snprintf(body_text, sizeof(body_text), "%s\n\n\"%s\"\n\n%s",
             e->ref, e->text, e->commentary);
    text_layer_set_text(s_body_layer, body_text);

    GSize text_size = graphics_text_layout_get_content_size(
        body_text, fonts_get_system_font(FONT_KEY_GOTHIC_28),
        GRect(0, 0, scroll_w - 8, 8000), GTextOverflowModeWordWrap, GTextAlignmentLeft);
    int body_h = text_size.h + 4;
    layer_set_frame(text_layer_get_layer(s_body_layer), GRect(4, 0, scroll_w - 8, body_h));
    layer_set_hidden(text_layer_get_layer(s_body_layer), false);
    layer_mark_dirty(text_layer_get_layer(s_body_layer));

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

    if (s_loading_active) {
        if (s_swap_in_progress) {
            loading_cancel();
        } else {
            loading_finish();
        }
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
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
        dict_write_int32(iter, KEY_ACTION, ACTION_SYNC_RANGE);
        dict_write_cstring(iter, KEY_START_DATE, start_date);
        dict_write_cstring(iter, KEY_END_DATE, end_date);
        dict_write_cstring(iter, KEY_LANGUAGE, s_language);
        dict_write_cstring(iter, KEY_LIB, s_lib);
        dict_write_cstring(iter, KEY_RSCONF, s_rsconf);
        app_message_outbox_send();
    } else {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Bulk sync outbox begin failed: %d", result);
        s_sync_in_progress = false;
        s_waiting_for_phone = false;
        update_ui();
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

    s_waiting_for_phone = true;
    update_ui();

    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result != APP_MSG_OK) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Fetch outbox begin failed: %d", result);
        s_waiting_for_phone = false;
        update_ui();
        return;
    }
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
        DayEntry *e = current_entry();
        if (e && e->has_data) {
            update_ui();
        } else {
            loading_cancel();
            Tuple *err_t = dict_find(iter, KEY_ERROR);
            static char err_msg[128];
            if (err_t) {
                snprintf(err_msg, sizeof(err_msg), "Fetch error: %s\nSELECT to retry.", err_t->value->cstring);
            } else {
                snprintf(err_msg, sizeof(err_msg), "Failed to load.\nPress SELECT to retry.");
            }
            GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
            text_layer_set_text(s_body_layer, err_msg);
            layer_set_frame(text_layer_get_layer(s_body_layer), GRect(4, 0, scroll_frame.size.w - 8, 200));
            layer_set_hidden(text_layer_get_layer(s_body_layer), false);
            scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_frame.size.w, scroll_frame.size.h));
            layer_set_hidden(s_bottom_arrow_layer, true);
            layer_set_hidden(s_next_bar, true);
            layer_mark_dirty(text_layer_get_layer(s_body_layer));
        }
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
    s_waiting_for_phone = false;
    update_ui();
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", reason);
    s_waiting_for_phone = false;
    s_sync_in_progress = false;
    update_ui();
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

    s_body_layer = text_layer_create(GRect(4, 0, bounds.size.w - 8, 8000));
    text_layer_set_font(s_body_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
    text_layer_set_text_color(s_body_layer, GColorBlack);
    text_layer_set_background_color(s_body_layer, GColorClear);
    text_layer_set_overflow_mode(s_body_layer, GTextOverflowModeWordWrap);

    s_scroll_layer = scroll_layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT + BANNER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT - BANNER_HEIGHT));
    scroll_layer_add_child(s_scroll_layer, text_layer_get_layer(s_body_layer));

    s_next_bar = layer_create(GRect(0, 0, bounds.size.w, BANNER_HEIGHT));
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

    s_loading_layer = layer_create(GRect(0, STATUS_BAR_LAYER_HEIGHT + BANNER_HEIGHT, bounds.size.w, bounds.size.h - STATUS_BAR_LAYER_HEIGHT - BANNER_HEIGHT));
    layer_set_update_proc(s_loading_layer, loading_update_proc);
    layer_set_hidden(s_loading_layer, true);
    layer_add_child(root, s_loading_layer);

    s_bottom_arrow_layer = layer_create(GRect(0, bounds.size.h - ARROW_HEIGHT, bounds.size.w, ARROW_HEIGHT));
    layer_set_update_proc(s_bottom_arrow_layer, bottom_arrow_update_proc);
    layer_set_hidden(s_bottom_arrow_layer, true);
    layer_add_child(root, s_bottom_arrow_layer);

    window_set_background_color(window, GColorWhite);

    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }

    update_ui();
}

static void window_unload(Window *window) {
    if (s_scroll_animation) {
        animation_unschedule(s_scroll_animation);
        s_scroll_animation = NULL;
    }
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
    loading_cancel();
    if (s_temp_bar) {
        layer_destroy(s_temp_bar);
        s_temp_bar = NULL;
    }
    text_layer_destroy(s_body_layer);
    scroll_layer_destroy(s_scroll_layer);
    layer_destroy(s_banner_layer);
    layer_destroy(s_bottom_arrow_layer);
    layer_destroy(s_next_bar);
    layer_destroy(s_loading_layer);
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

