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
#define KEY_TEXT_SIZE MESSAGE_KEY_text_size
#define KEY_LANGUAGE_LIST MESSAGE_KEY_language_list

#define PERSIST_KEY_LANGUAGE 1
#define PERSIST_KEY_CACHE_DAYS 2
#define PERSIST_KEY_LIB 3
#define PERSIST_KEY_RSCONF 4
/* Key 5 held a legacy single-blob cache that the firmware silently truncated
   (persist values are capped at 256 bytes); it is deleted on boot. */
#define PERSIST_KEY_CACHE 5
#define PERSIST_KEY_TEXT_SIZE 6
#define PERSIST_KEY_LANG_LIST 7
#define PERSIST_KEY_MIGRATED 8
/* Day records moved from base 100 (no language tag) to base 500 (language
   tag in the record header) in 2.4.0; the legacy range is wiped once. */
#define LEGACY_PERSIST_KEY_DAY_BASE 100
#define LEGACY_MAX_PERSIST_SLOTS 40
#define PERSIST_KEY_DAY_BASE 500

#if defined(PBL_PLATFORM_APLITE)
  /* First-generation Pebbles have 24 KiB app RAM. Persist carries the
     offline cache; RAM holds only the visible day. The record scratch buffer
     is also reused for rendered body text. */
  #define CACHE_SIZE 1
  #define ENTRY_REF_SIZE 64
  #define ENTRY_TEXT_SIZE 512
  #define ENTRY_COMMENTARY_SIZE 1152
  #define APPMSG_INBOX_SIZE 2048
  #define APPMSG_OUTBOX_SIZE 256
  #define MAX_WATCH_CACHE_DAYS 7
#else
  #define CACHE_SIZE 20
  #define ENTRY_REF_SIZE 64
  #define ENTRY_TEXT_SIZE 512
  #define ENTRY_COMMENTARY_SIZE 1152
  #define BODY_TEXT_SIZE 2200
  #define APPMSG_INBOX_SIZE 8192
  #define APPMSG_OUTBOX_SIZE 512
  #define MAX_WATCH_CACHE_DAYS 30
#endif
#define PAST_DAYS_KEEP 7
#define MIN_WATCH_CACHE_DAYS 7
#define DEFAULT_WATCH_CACHE_DAYS MAX_WATCH_CACHE_DAYS
#define PERSIST_CHUNK_SIZE 256
#define MAX_CHUNKS_PER_DAY 8
#define MAX_PERSIST_SLOTS 40
#define SCROLL_INCREMENT 36
#define SCROLL_REPEAT_INTERVAL_MS 220
#define ARROW_HEIGHT 19
#define BANNER_HEIGHT 36
#define SWAP_ANIM_DURATION_MS 220
#define REVEAL_ANIM_DURATION_MS 260
#define LOADING_FILL_DURATION_MS 2400
#define LOADING_DONE_DURATION_MS 180
#define TOUCH_SWAP_THRESHOLD 50
#define MAX_LANG_LIST 4
#define LANG_SIZE 12

#define BODY_MARGIN PBL_IF_ROUND_ELSE(12, 4)

#define COLOR_BANNER PBL_IF_COLOR_ELSE(GColorIndigo, GColorBlack)
#define COLOR_BANNER_TEXT GColorWhite
#define COLOR_NEXT_BAR COLOR_BANNER
#define COLOR_NEXT_BAR_TEXT GColorWhite

typedef struct {
    bool has_data;
    char date[11];
    char lang[LANG_SIZE];
    char ref[ENTRY_REF_SIZE];
    char text[ENTRY_TEXT_SIZE];
    char commentary[ENTRY_COMMENTARY_SIZE];
} DayEntry;

typedef struct {
    char lang[LANG_SIZE];
    char lib[12];
    char rsconf[4];
    char name[24];
} LangInfo;

static const int ACTION_FETCH = 1;
static const int ACTION_FETCH_RESULT = 2;
static const int ACTION_FETCH_ERROR = 3;
static const int ACTION_LANGUAGE_CHANGED = 4;
static const int ACTION_SYNC_RANGE = 5;
static const int ACTION_LANG_LIST = 6;
static const int ACTION_SETTINGS = 7;

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
static char s_language[LANG_SIZE];
static char s_lib[16];
static char s_rsconf[8];
static int s_cache_days;
static int s_text_size; /* 0 = standard, 1 = small */

static LangInfo s_lang_list[MAX_LANG_LIST];
static int s_lang_count = 0;
static Window *s_menu_window;
static MenuLayer *s_menu_layer;

static DayEntry s_cache[CACHE_SIZE];
static bool s_sync_in_progress = false;
static Animation *s_scroll_animation = NULL;
static int s_scroll_anim_start_y = 0;
static int s_scroll_anim_target_y = 0;
static AnimationImplementation s_scroll_anim_impl;

static DayEntry *get_day_anywhere(const char *date_str, const char *lang);
static DayEntry *alloc_ram_entry(void);
static int persist_find_slot(const char *date_str, const char *lang);
static void persist_dir_load(void);
static void persist_wipe_all(void);
static void prune_persist_cache(void);
static bool persist_in_retention_window(const char *date_str);
static void save_day_to_persist(const char *date, const char *lang,
                                const char *ref, const char *text,
                                const char *commentary);
static void update_ui(void);
static void request_from_phone(void);
static void reset_scroll_to_top(void);

static DayEntry *current_entry(void) {
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);

    return get_day_anywhere(date_str, s_language);
}

static DayEntry *find_cache_entry(const char *date_str, const char *lang) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (s_cache[i].has_data &&
            strcmp(s_cache[i].date, date_str) == 0 &&
            strcmp(s_cache[i].lang, lang) == 0) {
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

typedef struct {
    const char *today;
    const char *no_data;
    const char *fetch_error;
    const char *load_failed;
    const char *retry_hint;
    const char *weekdays[7];   /* Monday .. Sunday */
    const char *months[12];
    int date_style;            /* 0: "Fri July 17th", 1: "Fr 17. Juli", 2: "Ven 17 juillet" */
} LocaleStrings;

static const LocaleStrings LOCALE_EN = {
    "Today",
    "No data available.",
    "Fetch error",
    "Failed to load.",
    "Press SELECT to retry.",
    {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"},
    {"January", "February", "March", "April", "May", "June",
     "July", "August", "September", "October", "November", "December"},
    0
};

static const LocaleStrings LOCALE_DE = {
    "Heute",
    "Keine Daten verfügbar.",
    "Fehler beim Laden",
    "Laden fehlgeschlagen.",
    "Zum Wiederholen SELECT drücken.",
    {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"},
    {"Januar", "Februar", "März", "April", "Mai", "Juni",
     "Juli", "August", "September", "Oktober", "November", "Dezember"},
    1
};

static const LocaleStrings LOCALE_IT = {
    "Oggi",
    "Nessun dato disponibile.",
    "Errore di caricamento",
    "Caricamento non riuscito.",
    "Premi SELECT per riprovare.",
    {"Lun", "Mar", "Mer", "Gio", "Ven", "Sab", "Dom"},
    {"gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno",
     "luglio", "agosto", "settembre", "ottobre", "novembre", "dicembre"},
    2
};

static const LocaleStrings LOCALE_ES = {
    "Hoy",
    "No hay datos disponibles.",
    "Error al cargar",
    "No se pudo cargar.",
    "Pulsa SELECT para reintentar.",
    {"Lun", "Mar", "Mié", "Jue", "Vie", "Sáb", "Dom"},
    {"enero", "febrero", "marzo", "abril", "mayo", "junio",
     "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"},
    2
};

static const LocaleStrings LOCALE_FR = {
    "Aujourd'hui",
    "Aucune donnée disponible.",
    "Erreur de chargement",
    "Échec du chargement.",
    "Appuyez sur SELECT pour réessayer.",
    {"Lun", "Mar", "Mer", "Jeu", "Ven", "Sam", "Dim"},
    {"janvier", "février", "mars", "avril", "mai", "juin",
     "juillet", "août", "septembre", "octobre", "novembre", "décembre"},
    2
};

static const LocaleStrings *current_locale(void) {
    if (strncmp(s_language, "de", 2) == 0) return &LOCALE_DE;
    if (strncmp(s_language, "it", 2) == 0) return &LOCALE_IT;
    if (strncmp(s_language, "es", 2) == 0) return &LOCALE_ES;
    if (strncmp(s_language, "fr", 2) == 0) return &LOCALE_FR;
    return &LOCALE_EN;
}

/* Device language -> wol.jw.org defaults, used on first run (issue: preselect
   the watch's language). Codes from config/languages.json. */
typedef struct {
    const char *code;
    const char *lib;
    const char *rsconf;
} DeviceLocale;

static const DeviceLocale DEVICE_LOCALES[] = {
    {"de", "lp-x", "10"},
    {"es", "lp-s", "4"},
    {"fr", "lp-f", "1"},
    {"it", "lp-i", "1"},
    {"pt", "lp-t", "1"},
    {"nl", "lp-o", "1"},
    {"pl", "lp-p", "1"},
    {"ru", "lp-u", "1"},
    {"ja", "lp-j", "7"},
    {"ko", "lp-ko", "1"},
};

static void detect_device_language(char *lang_out, int lang_size,
                                   char *lib_out, int lib_size,
                                   char *rsconf_out, int rsconf_size) {
    char code[3] = {0, 0, 0};
    const char *sys = i18n_get_system_locale();
    if (sys && sys[0] && sys[1]) {
        char a = sys[0], b = sys[1];
        code[0] = (a >= 'A' && a <= 'Z') ? a + 32 : a;
        code[1] = (b >= 'A' && b <= 'Z') ? b + 32 : b;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "System locale: %s (code %s)", sys ? sys : "?", code);

    const DeviceLocale *match = NULL;
    for (unsigned int i = 0; i < ARRAY_LENGTH(DEVICE_LOCALES); i++) {
        if (strcmp(code, DEVICE_LOCALES[i].code) == 0) {
            match = &DEVICE_LOCALES[i];
            break;
        }
    }
    snprintf(lang_out, lang_size, "%s", match ? match->code : "en");
    snprintf(lib_out, lib_size, "%s", match ? match->lib : "lp-e");
    snprintf(rsconf_out, rsconf_size, "%s", match ? match->rsconf : "1");
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

static bool day_is_cached(const char *date_str, const char *lang) {
    return find_cache_entry(date_str, lang) != NULL || persist_find_slot(date_str, lang) >= 0;
}

static int future_days_per_language(void) {
    int languages = s_lang_count > 0 ? s_lang_count : 1;
    int days = (s_cache_days + 1) / languages - 1;
    return days < 1 ? 1 : days;
}

static int count_future_cached_days(const char *from_date) {
    int count = 0;
    int year, month, day;
    if (!parse_date(from_date, &year, &month, &day)) return 0;

    char next_date[11];
    add_days_to_date(year, month, day, 1, next_date, sizeof(next_date));

    int target = future_days_per_language();
    while (count < target) {
        if (!day_is_cached(next_date, s_language)) break;
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

/* Persistent day cache. Firmware caps each persist value at 256 bytes, so a
   day is stored as a packed record split into chunks across consecutive keys:
   key = PERSIST_KEY_DAY_BASE + slot * MAX_CHUNKS_PER_DAY + chunk.
   Record layout: date[11] | lang[LANG_SIZE] | ref_len u16 | text_len u16 | comm_len
   u16 (lengths include the NUL), then ref, text, commentary. Multiple
   languages coexist; each record carries its language. */

#define RECORD_HEADER_SIZE (11 + LANG_SIZE + 6)
#define MAX_RECORD_SIZE (RECORD_HEADER_SIZE + sizeof(((DayEntry *)0)->ref) + \
                         sizeof(((DayEntry *)0)->text) + sizeof(((DayEntry *)0)->commentary))

typedef struct {
    char date[11];
    char lang[LANG_SIZE];
} PersistDirEntry;

static PersistDirEntry s_persist_dir[MAX_PERSIST_SLOTS];
static uint8_t s_record_buf[MAX_RECORD_SIZE];

static uint32_t day_chunk_key(int slot, int chunk) {
    return PERSIST_KEY_DAY_BASE + (uint32_t)slot * MAX_CHUNKS_PER_DAY + (uint32_t)chunk;
}

static uint32_t legacy_day_chunk_key(int slot, int chunk) {
    return LEGACY_PERSIST_KEY_DAY_BASE + (uint32_t)slot * MAX_CHUNKS_PER_DAY + (uint32_t)chunk;
}

static bool valid_stored_date(const char *d) {
    for (int i = 0; i < 10; i++) {
        char c = d[i];
        if (c == '\0') return false;
        if (i == 4 || i == 7) {
            if (c != '-') return false;
        } else if (c < '0' || c > '9') {
            return false;
        }
    }
    return d[10] == '\0';
}

static bool valid_stored_lang(const char *l) {
    int len = 0;
    for (int i = 0; i < LANG_SIZE; i++) {
        char c = l[i];
        if (c == '\0') break;
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        len++;
    }
    return len >= 1 && len < LANG_SIZE;
}

static int persist_find_slot(const char *date_str, const char *lang) {
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (s_persist_dir[i].date[0] &&
            strcmp(s_persist_dir[i].date, date_str) == 0 &&
            strcmp(s_persist_dir[i].lang, lang) == 0) return i;
    }
    return -1;
}

static int persist_free_slot(void) {
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (!s_persist_dir[i].date[0]) return i;
    }
    return -1;
}

static void delete_persist_slot(int slot) {
    for (int j = 0; j < MAX_CHUNKS_PER_DAY; j++) {
        persist_delete(day_chunk_key(slot, j));
    }
    s_persist_dir[slot].date[0] = '\0';
}

static void persist_dir_load(void) {
    memset(s_persist_dir, 0, sizeof(s_persist_dir));
    uint8_t hdr[RECORD_HEADER_SIZE];
    int count = 0;
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (persist_read_data(day_chunk_key(i, 0), hdr, sizeof(hdr)) < (int)sizeof(hdr)) continue;
        if (!valid_stored_date((const char *)hdr)) continue;
        if (!valid_stored_lang((const char *)hdr + 11)) continue;
        uint16_t ref_len = hdr[11 + LANG_SIZE] | (hdr[12 + LANG_SIZE] << 8);
        uint16_t text_len = hdr[13 + LANG_SIZE] | (hdr[14 + LANG_SIZE] << 8);
        uint16_t comm_len = hdr[15 + LANG_SIZE] | (hdr[16 + LANG_SIZE] << 8);
        if (ref_len < 1 || ref_len > sizeof(((DayEntry *)0)->ref)) continue;
        if (text_len < 1 || text_len > sizeof(((DayEntry *)0)->text)) continue;
        if (comm_len < 1 || comm_len > sizeof(((DayEntry *)0)->commentary)) continue;
        memcpy(s_persist_dir[i].date, hdr, 11);
        memcpy(s_persist_dir[i].lang, hdr + 11, LANG_SIZE);
        count++;
    }
    APP_LOG(APP_LOG_LEVEL_INFO, "Persist dir loaded %d days", count);
}

/* Associate pre-2.4.0 records with the persisted current language instead of
   discarding a user's offline cache during upgrade. */
static bool migrate_legacy_cache(void) {
    bool all_done = true;
    for (int old_slot = 0; old_slot < LEGACY_MAX_PERSIST_SLOTS; old_slot++) {
        bool migrated = false;
        bool remove_legacy = true;
#if !defined(PBL_PLATFORM_APLITE)
        const int old_header_size = 17;
        uint8_t hdr[17];
        if (persist_read_data(legacy_day_chunk_key(old_slot, 0), hdr, sizeof(hdr)) == (int)sizeof(hdr) &&
            valid_stored_date((const char *)hdr)) {
            uint16_t ref_len = hdr[11] | (hdr[12] << 8);
            uint16_t text_len = hdr[13] | (hdr[14] << 8);
            uint16_t comm_len = hdr[15] | (hdr[16] << 8);
            uint32_t old_total = old_header_size + ref_len + text_len + comm_len;
            if (ref_len >= 1 && ref_len <= ENTRY_REF_SIZE &&
                text_len >= 1 && text_len <= ENTRY_TEXT_SIZE &&
                comm_len >= 1 && comm_len <= ENTRY_COMMENTARY_SIZE &&
                old_total + (RECORD_HEADER_SIZE - old_header_size) <= MAX_RECORD_SIZE &&
                persist_in_retention_window((const char *)hdr)) {
                bool complete = true;
                int chunk = 0;
                for (uint32_t pos = 0; pos < old_total; pos += PERSIST_CHUNK_SIZE, chunk++) {
                    uint32_t n = old_total - pos;
                    if (n > PERSIST_CHUNK_SIZE) n = PERSIST_CHUNK_SIZE;
                    if (persist_read_data(legacy_day_chunk_key(old_slot, chunk), s_record_buf + pos, n) < (int)n) {
                        complete = false;
                        break;
                    }
                }
                int new_slot = persist_free_slot();
                if (complete && new_slot < 0) {
                    remove_legacy = false;
                    all_done = false;
                } else if (complete) {
                    memmove(s_record_buf + RECORD_HEADER_SIZE,
                            s_record_buf + old_header_size,
                            old_total - old_header_size);
                    memset(s_record_buf + 11, 0, LANG_SIZE);
                    strncpy((char *)s_record_buf + 11, s_language, LANG_SIZE - 1);
                    s_record_buf[11 + LANG_SIZE] = ref_len & 0xff;
                    s_record_buf[12 + LANG_SIZE] = ref_len >> 8;
                    s_record_buf[13 + LANG_SIZE] = text_len & 0xff;
                    s_record_buf[14 + LANG_SIZE] = text_len >> 8;
                    s_record_buf[15 + LANG_SIZE] = comm_len & 0xff;
                    s_record_buf[16 + LANG_SIZE] = comm_len >> 8;
                    uint32_t new_total = old_total + RECORD_HEADER_SIZE - old_header_size;
                    migrated = true;
                    chunk = 0;
                    for (uint32_t pos = 0; pos < new_total; pos += PERSIST_CHUNK_SIZE, chunk++) {
                        uint32_t n = new_total - pos;
                        if (n > PERSIST_CHUNK_SIZE) n = PERSIST_CHUNK_SIZE;
                        if (persist_write_data(day_chunk_key(new_slot, chunk), s_record_buf + pos, n) < (int)n) {
                            migrated = false;
                            remove_legacy = false;
                            all_done = false;
                            for (int j = 0; j <= chunk; j++) persist_delete(day_chunk_key(new_slot, j));
                            break;
                        }
                    }
                    if (migrated) {
                        memcpy(s_persist_dir[new_slot].date, hdr, 11);
                        snprintf(s_persist_dir[new_slot].lang, sizeof(s_persist_dir[new_slot].lang), "%s", s_language);
                    }
                }
            }
        }
#endif
        /* Unsupported/out-of-window records are removed. Write failures keep
           the source so migration can retry on the next launch. */
        if (remove_legacy) {
            for (int chunk = 0; chunk < MAX_CHUNKS_PER_DAY; chunk++) {
                persist_delete(legacy_day_chunk_key(old_slot, chunk));
            }
        }
        (void)migrated;
    }
    return all_done;
}

static bool load_day_from_persist(const char *date_str, const char *lang, DayEntry *out) {
    int slot = persist_find_slot(date_str, lang);
    if (slot < 0) return false;

    uint8_t hdr[RECORD_HEADER_SIZE];
    if (persist_read_data(day_chunk_key(slot, 0), hdr, sizeof(hdr)) < (int)sizeof(hdr)) return false;
    uint16_t ref_len = hdr[11 + LANG_SIZE] | (hdr[12 + LANG_SIZE] << 8);
    uint16_t text_len = hdr[13 + LANG_SIZE] | (hdr[14 + LANG_SIZE] << 8);
    uint16_t comm_len = hdr[15 + LANG_SIZE] | (hdr[16 + LANG_SIZE] << 8);
    uint32_t total = RECORD_HEADER_SIZE + ref_len + text_len + comm_len;
    if (total > MAX_RECORD_SIZE) return false;

    int chunk = 0;
    for (uint32_t pos = 0; pos < total; pos += PERSIST_CHUNK_SIZE, chunk++) {
        uint32_t want = total - pos;
        if (want > PERSIST_CHUNK_SIZE) want = PERSIST_CHUNK_SIZE;
        if (persist_read_data(day_chunk_key(slot, chunk), s_record_buf + pos, want) < (int)want) {
            return false;
        }
    }

    memset(out, 0, sizeof(*out));
    strncpy(out->date, date_str, sizeof(out->date) - 1);
    strncpy(out->lang, lang, sizeof(out->lang) - 1);
    memcpy(out->ref, s_record_buf + RECORD_HEADER_SIZE, ref_len);
    out->ref[sizeof(out->ref) - 1] = '\0';
    memcpy(out->text, s_record_buf + RECORD_HEADER_SIZE + ref_len, text_len);
    out->text[sizeof(out->text) - 1] = '\0';
    memcpy(out->commentary, s_record_buf + RECORD_HEADER_SIZE + ref_len + text_len, comm_len);
    out->commentary[sizeof(out->commentary) - 1] = '\0';
    out->has_data = true;
    return true;
}

static bool persist_in_retention_window(const char *date_str) {
    int y, m, d;
    if (!parse_date(date_str, &y, &m, &d)) return false;
    int entry_days = date_to_days(y, m, d);
    int today_days = date_to_days(s_current_year, s_current_month, s_current_day);
    int languages = s_lang_count > 0 ? s_lang_count : 1;
    int past_days = PAST_DAYS_KEEP / languages;
    int future_days = (s_cache_days + 1) / languages - 1;
    if (past_days < 1) past_days = 1;
    if (future_days < 1) future_days = 1;
    return entry_days >= today_days - past_days &&
           entry_days <= today_days + future_days;
}

static void prune_persist_cache(void) {
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (!s_persist_dir[i].date[0]) continue;
        if (!persist_in_retention_window(s_persist_dir[i].date)) {
            delete_persist_slot(i);
        }
    }
}

static size_t utf8_prefix_len(const char *s, size_t max_bytes) {
    size_t n = strlen(s);
    if (n <= max_bytes) return n;
    n = max_bytes;
    while (n > 0 && (((uint8_t)s[n] & 0xc0) == 0x80)) n--;
    return n;
}

static uint16_t stored_string_len(const char *s, size_t capacity) {
    size_t n = utf8_prefix_len(s, capacity - 1);
    return (uint16_t)(n + 1);
}

static void save_day_to_persist(const char *date, const char *lang,
                                const char *ref, const char *text,
                                const char *commentary) {
    if (persist_find_slot(date, lang) >= 0) return;
    if (!persist_in_retention_window(date)) return;

    int slot = persist_free_slot();
    if (slot < 0) {
        prune_persist_cache();
        slot = persist_free_slot();
    }
    if (slot < 0) {
        /* All slots full with in-window days: drop the oldest one. */
        int oldest = -1;
        int oldest_days = 0;
        for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
            int y, m, d;
            if (!parse_date(s_persist_dir[i].date, &y, &m, &d)) { oldest = i; break; }
            int dd = date_to_days(y, m, d);
            if (oldest < 0 || dd < oldest_days) { oldest = i; oldest_days = dd; }
        }
        if (oldest < 0) return;
        delete_persist_slot(oldest);
        slot = oldest;
    }

    uint16_t ref_len = stored_string_len(ref, ENTRY_REF_SIZE);
    uint16_t text_len = stored_string_len(text, ENTRY_TEXT_SIZE);
    uint16_t comm_len = stored_string_len(commentary, ENTRY_COMMENTARY_SIZE);

    memset(s_record_buf, 0, RECORD_HEADER_SIZE);
    memcpy(s_record_buf, date, 11);
    strncpy((char *)s_record_buf + 11, lang, LANG_SIZE - 1);
    s_record_buf[11 + LANG_SIZE] = ref_len & 0xff;
    s_record_buf[12 + LANG_SIZE] = ref_len >> 8;
    s_record_buf[13 + LANG_SIZE] = text_len & 0xff;
    s_record_buf[14 + LANG_SIZE] = text_len >> 8;
    s_record_buf[15 + LANG_SIZE] = comm_len & 0xff;
    s_record_buf[16 + LANG_SIZE] = comm_len >> 8;
    uint32_t pos = RECORD_HEADER_SIZE;
    memcpy(s_record_buf + pos, ref, ref_len - 1);
    s_record_buf[pos + ref_len - 1] = '\0';
    pos += ref_len;
    memcpy(s_record_buf + pos, text, text_len - 1);
    s_record_buf[pos + text_len - 1] = '\0';
    pos += text_len;
    memcpy(s_record_buf + pos, commentary, comm_len - 1);
    s_record_buf[pos + comm_len - 1] = '\0';

    uint32_t total = RECORD_HEADER_SIZE + ref_len + text_len + comm_len;
    int chunk = 0;
    for (uint32_t pos = 0; pos < total; pos += PERSIST_CHUNK_SIZE, chunk++) {
        uint32_t n = total - pos;
        if (n > PERSIST_CHUNK_SIZE) n = PERSIST_CHUNK_SIZE;
        if (persist_write_data(day_chunk_key(slot, chunk), s_record_buf + pos, n) < (int)n) {
            APP_LOG(APP_LOG_LEVEL_ERROR, "Persist write failed for %s chunk %d", date, chunk);
            for (int j = 0; j <= chunk; j++) persist_delete(day_chunk_key(slot, j));
            return;
        }
    }
    memcpy(s_persist_dir[slot].date, date, 11);
    snprintf(s_persist_dir[slot].lang, sizeof(s_persist_dir[slot].lang), "%s", lang);
}

static void persist_wipe_all(void) {
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (s_persist_dir[i].date[0]) delete_persist_slot(i);
    }
}

static bool lang_in_list(const char *lang) {
    for (int i = 0; i < s_lang_count; i++) {
        if (strcmp(s_lang_list[i].lang, lang) == 0) return true;
    }
    return false;
}

/* Drop days in languages that are no longer wanted (the current language is
   always kept). Used when the phone sends a new language list. */
static void purge_languages_not_in_list(void) {
    if (s_lang_count == 0) return;
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_cache[i].has_data) continue;
        if (!lang_in_list(s_cache[i].lang) && strcmp(s_cache[i].lang, s_language) != 0) {
            s_cache[i].has_data = false;
        }
    }
    for (int i = 0; i < MAX_PERSIST_SLOTS; i++) {
        if (!s_persist_dir[i].date[0]) continue;
        if (!lang_in_list(s_persist_dir[i].lang) && strcmp(s_persist_dir[i].lang, s_language) != 0) {
            delete_persist_slot(i);
        }
    }
}

static DayEntry *alloc_ram_entry(void) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_cache[i].has_data) return &s_cache[i];
    }
    evict_old_entries();
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!s_cache[i].has_data) return &s_cache[i];
    }
    /* Keep today's current-language entry visible; another language's today
       can be evicted and reloaded from persist when the user switches. */
    int victim = -1;
    int victim_days = 0;
    int today_days = date_to_days(s_current_year, s_current_month, s_current_day);
    for (int i = 0; i < CACHE_SIZE; i++) {
        int y, m, d;
        if (!parse_date(s_cache[i].date, &y, &m, &d)) { victim = i; break; }
        int dd = date_to_days(y, m, d);
        if (dd == today_days && strcmp(s_cache[i].lang, s_language) == 0) continue;
        if (victim < 0 || dd < victim_days) { victim = i; victim_days = dd; }
    }
    if (victim < 0) return NULL;
    s_cache[victim].has_data = false;
    return &s_cache[victim];
}

static DayEntry *get_day_anywhere(const char *date_str, const char *lang) {
    DayEntry *e = find_cache_entry(date_str, lang);
    if (e) return e;
    if (persist_find_slot(date_str, lang) < 0) return NULL;
    e = alloc_ram_entry();
    if (!e) return NULL;
    if (!load_day_from_persist(date_str, lang, e)) {
        e->has_data = false;
        return NULL;
    }
    return e;
}

/* Body font from the text-size setting (issue #15): 0 = standard, 1 = small
   (roughly the size the official Pebble weather app uses). */
static GFont body_font(void) {
    return fonts_get_system_font(s_text_size == 1 ? FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_28);
}

static void copy_trunc(char *dst, size_t dst_size, const char *src) {
    size_t n = utf8_prefix_len(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Language switch list (issue #16). The phone sends a CSV:
   "lang|lib|rsconf|Name;lang|lib|rsconf|Name;..." including the primary
   language. Two or more entries enable the SELECT-button language menu. */
static void apply_lang_list(const char *csv) {
    s_lang_count = 0;
    if (!csv || !csv[0]) return;
    char buf[176];
    snprintf(buf, sizeof(buf), "%s", csv);
    char *p = buf;
    while (*p && s_lang_count < MAX_LANG_LIST) {
        /* One entry: lang|lib|rsconf|Name (name optional), ended by ';' */
        char *fields[4] = {NULL, NULL, NULL, NULL};
        int f = 0;
        fields[f++] = p;
        for (; *p && *p != ';'; p++) {
            if (*p == '|' && f < 4) {
                *p = '\0';
                fields[f++] = p + 1;
            }
        }
        if (*p == ';') {
            *p = '\0';
            p++;
        }
        if (fields[1] && fields[2] && valid_stored_lang(fields[0])) {
            LangInfo *li = &s_lang_list[s_lang_count++];
            copy_trunc(li->lang, sizeof(li->lang), fields[0]);
            copy_trunc(li->lib, sizeof(li->lib), fields[1]);
            copy_trunc(li->rsconf, sizeof(li->rsconf), fields[2]);
            copy_trunc(li->name, sizeof(li->name), (fields[3] && fields[3][0]) ? fields[3] : fields[0]);
        }
    }
}

static void switch_language_to(int idx) {
    if (idx < 0 || idx >= s_lang_count) return;
    LangInfo *li = &s_lang_list[idx];
    if (strcmp(li->lang, s_language) == 0) return;
    snprintf(s_language, sizeof(s_language), "%s", li->lang);
    snprintf(s_lib, sizeof(s_lib), "%s", li->lib);
    snprintf(s_rsconf, sizeof(s_rsconf), "%s", li->rsconf);
    persist_write_string(PERSIST_KEY_LANGUAGE, s_language);
    persist_write_string(PERSIST_KEY_LIB, s_lib);
    persist_write_string(PERSIST_KEY_RSCONF, s_rsconf);
    s_waiting_for_phone = false;
    reset_scroll_to_top();
    update_ui();
    request_from_phone();
}

static uint16_t menu_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *context) {
    return s_lang_count;
}

static void menu_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *context) {
    LangInfo *li = &s_lang_list[cell_index->row];
    bool current = strcmp(li->lang, s_language) == 0;
    menu_cell_basic_draw(ctx, cell_layer, li->name, current ? "Current" : li->lang, NULL);
}

static void menu_select_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *context) {
    switch_language_to(cell_index->row);
    window_stack_pop(true);
}

static void menu_window_unload(Window *window) {
    menu_layer_destroy(s_menu_layer);
    s_menu_layer = NULL;
    window_destroy(s_menu_window);
    s_menu_window = NULL;
}

static void open_lang_menu(void) {
    if (s_menu_window || s_lang_count < 2) return;
    s_menu_window = window_create();
    window_set_window_handlers(s_menu_window, (WindowHandlers) {
        .unload = menu_window_unload,
    });
    Layer *root = window_get_root_layer(s_menu_window);
    GRect bounds = layer_get_bounds(root);
    s_menu_layer = menu_layer_create(bounds);
    menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
        .get_num_rows = menu_get_num_rows,
        .draw_row = menu_draw_row,
        .select_click = menu_select_callback,
    });
    menu_layer_set_click_config_onto_window(s_menu_layer, s_menu_window);
    layer_add_child(root, menu_layer_get_layer(s_menu_layer));
    window_stack_push(s_menu_window, true);
}

static void load_previous_day(void);
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
        prune_persist_cache();
        s_waiting_for_phone = false;
        update_ui();
        request_from_phone();
    }
}

static void prv_format_date_str(char *buf, int size, int y, int m, int d) {
    const LocaleStrings *loc = current_locale();
    const char *mon = (m >= 1 && m <= 12) ? loc->months[m - 1] : "?";
    const char *day_name = loc->weekdays[weekday_of(y, m, d)];
    if (loc->date_style == 0) {
        const char *sfx = "th";
        if (d >= 11 && d <= 13) sfx = "th";
        else if (d % 10 == 1) sfx = "st";
        else if (d % 10 == 2) sfx = "nd";
        else if (d % 10 == 3) sfx = "rd";
        snprintf(buf, size, "%s %s %d%s", day_name, mon, d, sfx);
    } else if (loc->date_style == 1) {
        snprintf(buf, size, "%s %d. %s", day_name, d, mon);
    } else {
        snprintf(buf, size, "%s %d %s", day_name, d, mon);
    }
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
    DayEntry *e = get_day_anywhere(prev_date, s_language);

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
    s_waiting_for_phone = false;
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
    if (s_lang_count >= 2) {
        open_lang_menu();
        return;
    }
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
        s_waiting_for_phone = (!e || !e->has_data);
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

/* Touch input exists only on emery (and gabbro); on other platforms the SDK
   stubs the touch service, so the handler would be dead code. */
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
#define HAS_TOUCH 1
#endif

#ifdef HAS_TOUCH
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
        /* content follows the finger 1:1 (offset is <= 0, y grows downward) */
        int raw = s_touch_start_offset + (event->y - (int)s_touch_start_y);
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
#endif /* HAS_TOUCH */

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_UP, SCROLL_REPEAT_INTERVAL_MS, up_click_handler);
    window_single_repeating_click_subscribe(BUTTON_ID_DOWN, SCROLL_REPEAT_INTERVAL_MS, down_click_handler);
    window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

static void update_ui(void) {
    const LocaleStrings *loc = current_locale();
    if (is_today(s_current_year, s_current_month, s_current_day)) {
        snprintf(s_banner_text, sizeof(s_banner_text), "%s", loc->today);
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
        static char no_data_msg[160];
        snprintf(no_data_msg, sizeof(no_data_msg), "%s\n%s", loc->no_data, loc->retry_hint);
        text_layer_set_text(s_body_layer, no_data_msg);
        layer_set_frame(text_layer_get_layer(s_body_layer), GRect(BODY_MARGIN, 0, scroll_w - 2 * BODY_MARGIN, 200));
        layer_set_hidden(text_layer_get_layer(s_body_layer), false);
        scroll_layer_set_content_size(s_scroll_layer, GSize(scroll_w, scroll_frame.size.h));
        layer_set_hidden(s_bottom_arrow_layer, true);
        layer_set_hidden(s_next_bar, true);
        layer_mark_dirty(text_layer_get_layer(s_body_layer));
        return;
    }

    /* Max possible: ref + text + commentary + 6 separators */
#if defined(PBL_PLATFORM_APLITE)
    char *body_text = (char *)s_record_buf;
    size_t body_capacity = sizeof(s_record_buf);
#else
    static char body_text[BODY_TEXT_SIZE];
    size_t body_capacity = sizeof(body_text);
#endif
    snprintf(body_text, body_capacity, "%s\n\n\"%s\"\n\n%s",
             e->ref, e->text, e->commentary);
    text_layer_set_text(s_body_layer, body_text);

    GSize text_size = graphics_text_layout_get_content_size(
        body_text, body_font(),
        GRect(0, 0, scroll_w - 2 * BODY_MARGIN, 8000), GTextOverflowModeWordWrap, GTextAlignmentLeft);
    int body_h = text_size.h + 4;
    layer_set_frame(text_layer_get_layer(s_body_layer), GRect(BODY_MARGIN, 0, scroll_w - 2 * BODY_MARGIN, body_h));
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
    add_days_to_date(year, month, day, future_days_per_language(), end_date, sizeof(end_date));
    
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

    DayEntry *cached = get_day_anywhere(date_str, s_language);
    if (cached) {
        s_waiting_for_phone = false;
        update_ui();

        int future_count = count_future_cached_days(date_str);
        if (future_count < future_days_per_language()) {
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
        Tuple *lang_t = dict_find(iter, KEY_LANGUAGE);
        const char *lang = lang_t ? lang_t->value->cstring : s_language;

        /* Persist every in-window result even when the small Aplite RAM cache
           has no free entry. It can be loaded on demand later. */
        save_day_to_persist(date_str, lang, ref_t->value->cstring,
                            text_t->value->cstring, comm_t->value->cstring);

        DayEntry *e = find_cache_entry(date_str, lang);
        if (!e) e = alloc_ram_entry();
        if (e) {
            strncpy(e->date, date_str, sizeof(e->date) - 1);
            e->date[sizeof(e->date) - 1] = '\0';
            strncpy(e->lang, lang, sizeof(e->lang) - 1);
            e->lang[sizeof(e->lang) - 1] = '\0';
            copy_trunc(e->ref, sizeof(e->ref), ref_t->value->cstring);
            copy_trunc(e->text, sizeof(e->text), text_t->value->cstring);
            copy_trunc(e->commentary, sizeof(e->commentary), comm_t->value->cstring);
            e->has_data = true;
        }

        int year, month, day;
        if (parse_date(date_str, &year, &month, &day)) {
            if (year == s_current_year && month == s_current_month && day == s_current_day &&
                strcmp(lang, s_language) == 0) {
                s_waiting_for_phone = false;
                scroll_layer_set_content_offset(s_scroll_layer, GPoint(0, 0), false);
                update_ui();
#if defined(PBL_PLATFORM_APLITE)
            } else {
                /* Persistence uses the same scratch buffer as rendered text. */
                update_ui();
#endif
            }
        }
    } else if (action == ACTION_FETCH_ERROR) {
        s_waiting_for_phone = false;
        DayEntry *e = current_entry();
        if (e && e->has_data) {
            update_ui();
        } else {
            loading_cancel();
            const LocaleStrings *loc = current_locale();
            Tuple *err_t = dict_find(iter, KEY_ERROR);
            static char err_msg[192];
            if (err_t) {
                snprintf(err_msg, sizeof(err_msg), "%s: %s\n%s", loc->fetch_error, err_t->value->cstring, loc->retry_hint);
            } else {
                snprintf(err_msg, sizeof(err_msg), "%s\n%s", loc->load_failed, loc->retry_hint);
            }
            GRect scroll_frame = layer_get_frame(scroll_layer_get_layer(s_scroll_layer));
            text_layer_set_text(s_body_layer, err_msg);
            layer_set_frame(text_layer_get_layer(s_body_layer), GRect(BODY_MARGIN, 0, scroll_frame.size.w - 2 * BODY_MARGIN, 200));
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
        Tuple *ll_t = dict_find(iter, KEY_LANGUAGE_LIST);
        Tuple *ts_t = dict_find(iter, KEY_TEXT_SIZE);
        if (ll_t) {
            apply_lang_list(ll_t->value->cstring);
            persist_write_string(PERSIST_KEY_LANG_LIST, ll_t->value->cstring);
        }
        if (lang_t) {
            copy_trunc(s_language, sizeof(s_language), lang_t->value->cstring);
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
            if (s_cache_days < MIN_WATCH_CACHE_DAYS) s_cache_days = MIN_WATCH_CACHE_DAYS;
            if (s_cache_days > MAX_WATCH_CACHE_DAYS) s_cache_days = MAX_WATCH_CACHE_DAYS;
            persist_write_int(PERSIST_KEY_CACHE_DAYS, s_cache_days);
        }
        if (ts_t) {
            s_text_size = ts_t->value->int32 == 1 ? 1 : 0;
            persist_write_int(PERSIST_KEY_TEXT_SIZE, s_text_size);
            text_layer_set_font(s_body_layer, body_font());
        }
        if (s_lang_count > 0) {
            /* Multi-language: keep days of languages still in the list. */
            purge_languages_not_in_list();
        } else {
            for (int i = 0; i < CACHE_SIZE; i++) s_cache[i].has_data = false;
            persist_wipe_all();
        }
        prune_persist_cache();
        s_waiting_for_phone = true;
        update_ui();
        request_from_phone();
    } else if (action == ACTION_LANG_LIST) {
        Tuple *ll_t = dict_find(iter, KEY_LANGUAGE_LIST);
        if (ll_t) {
            apply_lang_list(ll_t->value->cstring);
            persist_write_string(PERSIST_KEY_LANG_LIST, ll_t->value->cstring);
            purge_languages_not_in_list();
            prune_persist_cache();
            update_ui();
        }
    } else if (action == ACTION_SETTINGS) {
        Tuple *ts_t = dict_find(iter, KEY_TEXT_SIZE);
        if (ts_t) {
            int ts = ts_t->value->int32 == 1 ? 1 : 0;
            if (ts != s_text_size) {
                s_text_size = ts;
                persist_write_int(PERSIST_KEY_TEXT_SIZE, s_text_size);
                text_layer_set_font(s_body_layer, body_font());
                update_ui();
            }
        }
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

    s_body_layer = text_layer_create(GRect(BODY_MARGIN, 0, bounds.size.w - 2 * BODY_MARGIN, 8000));
    text_layer_set_font(s_body_layer, body_font());
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

#ifdef HAS_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_subscribe(touch_handler, NULL);
    }
#endif

    update_ui();
}

static void window_unload(Window *window) {
    if (s_scroll_animation) {
        animation_unschedule(s_scroll_animation);
        s_scroll_animation = NULL;
    }
#ifdef HAS_TOUCH
    if (touch_service_is_enabled()) {
        touch_service_unsubscribe();
    }
#endif
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

    char def_lang[8], def_lib[16], def_rsconf[8];
    detect_device_language(def_lang, sizeof(def_lang), def_lib, sizeof(def_lib), def_rsconf, sizeof(def_rsconf));

    if (persist_exists(PERSIST_KEY_LANGUAGE)) {
        persist_read_string(PERSIST_KEY_LANGUAGE, s_language, sizeof(s_language));
    } else {
        snprintf(s_language, sizeof(s_language), "%s", def_lang);
    }

    if (persist_exists(PERSIST_KEY_LIB)) {
        persist_read_string(PERSIST_KEY_LIB, s_lib, sizeof(s_lib));
    } else {
        snprintf(s_lib, sizeof(s_lib), "%s", def_lib);
    }

    if (persist_exists(PERSIST_KEY_RSCONF)) {
        persist_read_string(PERSIST_KEY_RSCONF, s_rsconf, sizeof(s_rsconf));
    } else {
        snprintf(s_rsconf, sizeof(s_rsconf), "%s", def_rsconf);
    }

    if (persist_exists(PERSIST_KEY_CACHE_DAYS)) {
        s_cache_days = persist_read_int(PERSIST_KEY_CACHE_DAYS);
    } else {
        s_cache_days = DEFAULT_WATCH_CACHE_DAYS;
    }
    if (s_cache_days < MIN_WATCH_CACHE_DAYS) s_cache_days = MIN_WATCH_CACHE_DAYS;
    if (s_cache_days > MAX_WATCH_CACHE_DAYS) s_cache_days = MAX_WATCH_CACHE_DAYS;

    if (persist_exists(PERSIST_KEY_TEXT_SIZE)) {
        s_text_size = persist_read_int(PERSIST_KEY_TEXT_SIZE) == 1 ? 1 : 0;
    } else {
        s_text_size = 0;
    }

    if (persist_exists(PERSIST_KEY_LANG_LIST)) {
        char csv[176];
        if (persist_read_string(PERSIST_KEY_LANG_LIST, csv, sizeof(csv)) > 0) {
            apply_lang_list(csv);
        }
    }

    /* Remove the legacy single-blob cache (firmware truncated it to 256 bytes,
       so it only ever held a corrupt fragment), then load the chunked cache. */
    persist_delete(PERSIST_KEY_CACHE);
    persist_dir_load();
    if (!persist_exists(PERSIST_KEY_MIGRATED)) {
        if (migrate_legacy_cache()) persist_write_int(PERSIST_KEY_MIGRATED, 1);
    }
    prune_persist_cache();

    /* Preload today (and tomorrow) from persist so the UI is instant and the
       app works with no phone connection at all. */
    char date_str[11];
    snprintf(date_str, sizeof(date_str), "%d-%02d-%02d", s_current_year, s_current_month, s_current_day);
    get_day_anywhere(date_str, s_language);
    char next_date[11];
    add_days_to_date(s_current_year, s_current_month, s_current_day, 1, next_date, sizeof(next_date));
    get_day_anywhere(next_date, s_language);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_inbox_dropped(inbox_dropped_handler);
    app_message_register_outbox_failed(outbox_failed_handler);
    app_message_register_outbox_sent(outbox_sent_handler);
    app_message_open(APPMSG_INBOX_SIZE, APPMSG_OUTBOX_SIZE);

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
