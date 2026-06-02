#include "core/app_manager.h"
#include "core/context.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "drivers/board_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "pn532.h"
#include "system/key_store.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"

static const char *TAG = "keys_app";

typedef enum {
    KEYS_VIEW_LIST = 0,
    KEYS_VIEW_ACTIONS,
    KEYS_VIEW_SCAN,
    KEYS_VIEW_EMULATE,
    KEYS_VIEW_RENAME,
    KEYS_VIEW_DETAILS,
    KEYS_VIEW_MESSAGE,
} keys_view_t;

typedef struct {
    core_screen_t screen;
    key_store_record_t records[KEY_STORE_MAX_KEYS];
    size_t count;
    size_t selected;
    size_t action_selected;
    size_t active_index;
    keys_view_t view;
    keys_view_t message_return;
    keys_view_t scan_return;
    char message_title[18];
    char message_body[32];
    char rename_name[KEY_STORE_NAME_LEN];
    size_t rename_cursor;
    uint32_t scan_elapsed_ms;
    bool is_scanning;
    bool is_emulating;
} keys_app_state_t;

static keys_app_state_t s_keys;
static pn532_t *s_pn532;
static bool s_pn532_ready;

#define PN532_SPI_CLOCK_HZ 100000

static const char *ACTIONS[] = {
    "Emulate",
    "Rename",
    "Rewrite",
    "Details",
    "Delete",
};

static const char RENAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";

static void set_message(keys_app_state_t *state, const char *title, const char *body, keys_view_t return_view)
{
    snprintf(state->message_title, sizeof(state->message_title), "%s", title);
    snprintf(state->message_body, sizeof(state->message_body), "%s", body);
    state->message_return = return_view;
    state->view = KEYS_VIEW_MESSAGE;
}

static void reload_keys(keys_app_state_t *state)
{
    size_t count = 0;
    if (key_store_load_all(state->records, KEY_STORE_MAX_KEYS, &count) == ESP_OK) {
        state->count = count;
    } else {
        state->count = 0;
    }
    if (state->selected > state->count) {
        state->selected = state->count;
    }
    if (state->active_index >= state->count && state->active_index != (size_t)-1) {
        state->active_index = state->count == 0 ? (size_t)-1 : state->count - 1;
    }
}

static esp_err_t ensure_pn532(void)
{
    if (s_pn532_ready) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "PN532 SPI init host=%d sck=%d miso=%d mosi=%d cs=%d clock=%dHz",
             SPI2_HOST, BOARD_PIN_NFC_SPI_SCK, BOARD_PIN_NFC_SPI_MISO,
             BOARD_PIN_NFC_SPI_MOSI, BOARD_PIN_NFC_SPI_CS, PN532_SPI_CLOCK_HZ);
    pn532_bus_t *bus = pn532_spi_init(SPI2_HOST, BOARD_PIN_NFC_SPI_SCK, BOARD_PIN_NFC_SPI_MISO,
                                      BOARD_PIN_NFC_SPI_MOSI, BOARD_PIN_NFC_SPI_CS, PN532_SPI_CLOCK_HZ);
    if (bus == NULL) {
        ESP_LOGW(TAG, "pn532 spi bus init failed");
        return ESP_FAIL;
    }

    s_pn532 = pn532_init(bus, GPIO_NUM_NC, GPIO_NUM_NC);
    if (s_pn532 == NULL) {
        pn532_bus_destroy(bus);
        ESP_LOGW(TAG, "pn532 init failed");
        return ESP_FAIL;
    }

    uint32_t firmware = pn532_get_firmware_version(s_pn532);
    if (firmware == 0) {
        pn532_deinit(s_pn532, true);
        s_pn532 = NULL;
        ESP_LOGW(TAG, "pn532 firmware read failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PN532 firmware=0x%08lx", (unsigned long)firmware);
    s_pn532_ready = true;
    return ESP_OK;
}

static void begin_scan(core_context_t *ctx, keys_app_state_t *state, size_t active_index, keys_view_t return_view)
{
    esp_err_t err = ensure_pn532();
    if (err != ESP_OK) {
        set_message(state, "NFC ERROR", esp_err_to_name(err), return_view);
        core_nav_mark_dirty(&ctx->nav);
        return;
    }

    state->active_index = active_index;
    state->scan_return = return_view;
    state->scan_elapsed_ms = 250;
    state->is_scanning = true;
    state->view = KEYS_VIEW_SCAN;
    core_nav_mark_dirty(&ctx->nav);
}

static void fill_record_from_uid(key_store_record_t *record, const pn532_uid_t *uid, const char *name)
{
    memset(record, 0, sizeof(*record));
    snprintf(record->name, sizeof(record->name), "%s", name);
    record->type = KEY_STORE_TYPE_ISO14443A_UID;
    size_t uid_len = 0;
    if (uid != NULL && uid->uid_length > 0) {
        uid_len = (size_t)uid->uid_length;
    }
    if (uid_len > KEY_STORE_UID_MAX_LEN) {
        uid_len = KEY_STORE_UID_MAX_LEN;
    }
    record->uid_len = (uint8_t)uid_len;
    if (uid_len > 0) {
        memcpy(record->uid, uid->uid, uid_len);
    }
    record->atqa = uid != NULL ? uid->atqa : 0;
    record->sak = uid != NULL ? uid->sak : 0;
}

static void handle_scanned_uid(core_context_t *ctx, keys_app_state_t *state, const pn532_uid_t *uid)
{
    state->is_scanning = false;

    if (state->active_index == (size_t)-1) {
        char name[KEY_STORE_NAME_LEN];
        snprintf(name, sizeof(name), "KEY %02u", (unsigned)(state->count + 1));

        key_store_record_t record;
        fill_record_from_uid(&record, uid, name);
        esp_err_t err = key_store_add(&record);
        reload_keys(state);
        set_message(state, err == ESP_OK ? "SAVED" : "SAVE ERROR",
                    err == ESP_OK ? "UID saved to flash" : esp_err_to_name(err), KEYS_VIEW_LIST);
    } else if (state->active_index < state->count) {
        key_store_record_t record = state->records[state->active_index];
        char name[KEY_STORE_NAME_LEN];
        snprintf(name, sizeof(name), "%s", record.name);
        fill_record_from_uid(&record, uid, name);

        esp_err_t err = key_store_update(state->active_index, &record);
        reload_keys(state);
        set_message(state, err == ESP_OK ? "UPDATED" : "WRITE ERROR",
                    err == ESP_OK ? "Stored UID replaced" : esp_err_to_name(err), KEYS_VIEW_ACTIONS);
    }
    core_nav_mark_dirty(&ctx->nav);
}

static char next_rename_char(char current, int delta)
{
    const size_t len = sizeof(RENAME_CHARS) - 1;
    size_t index = 0;
    for (size_t i = 0; i < len; ++i) {
        if (RENAME_CHARS[i] == current) {
            index = i;
            break;
        }
    }
    if (delta > 0) {
        index = (index + 1) % len;
    } else {
        index = (index + len - 1) % len;
    }
    return RENAME_CHARS[index];
}

static void format_uid_hex(const key_store_record_t *record, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    size_t used = 0;
    for (uint8_t i = 0; record != NULL && i < record->uid_len; ++i) {
        int written = snprintf(out + used, out_size - used, "%02X", record->uid[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

static void format_uid_tail(const key_store_record_t *record, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (record == NULL || record->uid_len == 0) {
        return;
    }

    uint8_t start = record->uid_len > 4 ? record->uid_len - 4 : 0;
    size_t used = 0;
    for (uint8_t i = start; i < record->uid_len; ++i) {
        int written = snprintf(out + used, out_size - used, "%02X", record->uid[i]);
        if (written < 0 || (size_t)written >= out_size - used) {
            out[out_size - 1] = '\0';
            return;
        }
        used += (size_t)written;
    }
}

static void save_rename(core_context_t *ctx, keys_app_state_t *state)
{
    state->rename_name[KEY_STORE_NAME_LEN - 1] = '\0';
    esp_err_t err = key_store_rename(state->active_index, state->rename_name);
    reload_keys(state);
    set_message(state, err == ESP_OK ? "RENAMED" : "NAME ERROR",
                err == ESP_OK ? "Name saved" : esp_err_to_name(err), KEYS_VIEW_ACTIONS);
    core_nav_mark_dirty(&ctx->nav);
}

static void emulate_active(core_context_t *ctx, keys_app_state_t *state)
{
    if (state->active_index >= state->count) {
        return;
    }
    set_message(state, "EMULATE", "Not supported yet", KEYS_VIEW_ACTIONS);
    core_nav_mark_dirty(&ctx->nav);
}

static bool keys_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    keys_app_state_t *state = (keys_app_state_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    if (state->view == KEYS_VIEW_LIST) {
        const size_t entries = state->count + 1;
        switch (event->action) {
        case CORE_INPUT_BACK:
            (void)core_nav_pop(ctx, &ctx->nav);
            return true;
        case CORE_INPUT_UP:
            state->selected = (state->selected + entries - 1) % entries;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_DOWN:
            state->selected = (state->selected + 1) % entries;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_SELECT:
            if (state->selected == 0) {
                begin_scan(ctx, state, (size_t)-1, KEYS_VIEW_LIST);
            } else {
                state->active_index = state->selected - 1;
                state->action_selected = 0;
                state->view = KEYS_VIEW_ACTIONS;
                core_nav_mark_dirty(&ctx->nav);
            }
            return true;
        default:
            return false;
        }
    }

    if (state->view == KEYS_VIEW_ACTIONS) {
        const size_t action_count = sizeof(ACTIONS) / sizeof(ACTIONS[0]);
        switch (event->action) {
        case CORE_INPUT_BACK:
            state->view = KEYS_VIEW_LIST;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_UP:
            state->action_selected = (state->action_selected + action_count - 1) % action_count;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_DOWN:
            state->action_selected = (state->action_selected + 1) % action_count;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_SELECT:
            if (state->action_selected == 0) {
                emulate_active(ctx, state);
            } else if (state->action_selected == 1 && state->active_index < state->count) {
                snprintf(state->rename_name, sizeof(state->rename_name), "%s",
                         state->records[state->active_index].name);
                state->rename_cursor = 0;
                state->view = KEYS_VIEW_RENAME;
                core_nav_mark_dirty(&ctx->nav);
            } else if (state->action_selected == 2) {
                begin_scan(ctx, state, state->active_index, KEYS_VIEW_ACTIONS);
            } else if (state->action_selected == 3) {
                state->view = KEYS_VIEW_DETAILS;
                core_nav_mark_dirty(&ctx->nav);
            } else if (state->action_selected == 4) {
                esp_err_t err = key_store_delete(state->active_index);
                reload_keys(state);
                set_message(state, err == ESP_OK ? "DELETED" : "DEL ERROR",
                            err == ESP_OK ? "Record removed" : esp_err_to_name(err), KEYS_VIEW_LIST);
            }
            return true;
        default:
            return false;
        }
    }

    if (state->view == KEYS_VIEW_RENAME) {
        switch (event->action) {
        case CORE_INPUT_BACK:
            state->view = KEYS_VIEW_ACTIONS;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_LEFT:
            state->rename_cursor = state->rename_cursor == 0 ? KEY_STORE_NAME_LEN - 2 : state->rename_cursor - 1;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_RIGHT:
            state->rename_cursor = (state->rename_cursor + 1) % (KEY_STORE_NAME_LEN - 1);
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_UP:
            state->rename_name[state->rename_cursor] = next_rename_char(state->rename_name[state->rename_cursor], 1);
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_DOWN:
            state->rename_name[state->rename_cursor] = next_rename_char(state->rename_name[state->rename_cursor], -1);
            core_nav_mark_dirty(&ctx->nav);
            return true;
        case CORE_INPUT_SELECT:
            save_rename(ctx, state);
            return true;
        default:
            return false;
        }
    }

    if (state->view == KEYS_VIEW_SCAN) {
        if (event->action == CORE_INPUT_BACK) {
            state->is_scanning = false;
            state->view = state->scan_return;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        }
        return false;
    }

    if (state->view == KEYS_VIEW_EMULATE) {
        if (event->action == CORE_INPUT_BACK || event->action == CORE_INPUT_SELECT) {
            state->is_emulating = false;
            state->view = KEYS_VIEW_ACTIONS;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        }
        return false;
    }

    if (state->view == KEYS_VIEW_DETAILS) {
        if (event->action == CORE_INPUT_BACK || event->action == CORE_INPUT_SELECT) {
            state->view = KEYS_VIEW_ACTIONS;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        }
        return false;
    }

    if (state->view == KEYS_VIEW_MESSAGE) {
        if (event->action == CORE_INPUT_BACK || event->action == CORE_INPUT_SELECT) {
            state->view = state->message_return;
            core_nav_mark_dirty(&ctx->nav);
            return true;
        }
        return false;
    }

    return false;
}

static void keys_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms)
{
    keys_app_state_t *state = (keys_app_state_t *)screen->user_data;
    if (state->view != KEYS_VIEW_SCAN || !state->is_scanning) {
        return;
    }

    state->scan_elapsed_ms += dt_ms;
    if (state->scan_elapsed_ms < 250) {
        return;
    }
    state->scan_elapsed_ms = 0;

    pn532_uids_array_t *uids = pn532_14443_get_all_uids(s_pn532);
    if (uids != NULL && uids->uids_count > 0) {
        handle_scanned_uid(ctx, state, &uids->uids[0]);
        free(uids);
    } else {
        free(uids);
    }
}

static void draw_row(ui_t *ui, int y, const char *left, const char *right, bool selected)
{
    if (selected) {
        ui_fill_rect(ui, 1, y - 2, 126, 11, true);
    }
    ui_draw_text(ui, 4, y, left, !selected);
    if (right != NULL) {
        ui_draw_text_aligned(ui, 72, y, 52, right, UI_ALIGN_RIGHT, !selected);
    }
}

static void draw_list(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "KEYS");
    const size_t entries = state->count + 1;
    size_t top = state->selected > 3 ? state->selected - 3 : 0;
    for (size_t row = 0; row < 4 && top + row < entries; ++row) {
        size_t index = top + row;
        char uid[30];
        int y = 16 + (int)row * 11;
        if (index == 0) {
            draw_row(ui, y, "+ Scan new", NULL, state->selected == index);
        } else {
            format_uid_tail(&state->records[index - 1], uid, sizeof(uid));
            draw_row(ui, y, state->records[index - 1].name, uid, state->selected == index);
        }
    }
    if (state->count == 0) {
        ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "SELECT: scan tag", UI_ALIGN_CENTER, true);
    }
}

static void draw_actions(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "KEY ACTIONS");
    if (state->active_index >= state->count) {
        ui_widget_empty_state(ui, "NO KEY", "BACK");
        return;
    }
    ui_draw_text(ui, 4, 13, state->records[state->active_index].name, true);
    const size_t action_count = sizeof(ACTIONS) / sizeof(ACTIONS[0]);
    size_t top = state->action_selected > 3 ? state->action_selected - 3 : 0;
    for (size_t row = 0; row < 4 && top + row < action_count; ++row) {
        int y = 25 + (int)row * 10;
        draw_row(ui, y, ACTIONS[top + row], NULL, state->action_selected == top + row);
    }
}

static void draw_scan(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, state->active_index == (size_t)-1 ? "NEW KEY" : "REWRITE");
    ui_draw_text_aligned(ui, 0, 24, UI_WIDTH, "Hold tag near", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 36, UI_WIDTH, "PN532 antenna", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "BACK: cancel", UI_ALIGN_CENTER, true);
}

static void draw_emulate(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "EMULATE");
    if (state->active_index >= state->count) {
        ui_widget_empty_state(ui, "NO KEY", "BACK");
        return;
    }
    char uid[32];
    format_uid_hex(&state->records[state->active_index], uid, sizeof(uid));
    ui_draw_text_aligned(ui, 0, 18, UI_WIDTH, state->records[state->active_index].name, UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 32, UI_WIDTH, uid, UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "BACK: stop", UI_ALIGN_CENTER, true);
}

static void draw_rename(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "RENAME");
    ui_draw_text(ui, 4, 20, state->rename_name, true);
    int cursor_x = 4 + (int)state->rename_cursor * 6;
    ui_draw_hline(ui, cursor_x, 29, 5, true);
    ui_draw_text_aligned(ui, 0, 43, UI_WIDTH, "< > pos  UP/DN char", UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "SELECT: save", UI_ALIGN_CENTER, true);
}

static void draw_details(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "DETAILS");
    if (state->active_index >= state->count) {
        ui_widget_empty_state(ui, "NO KEY", "BACK");
        return;
    }
    key_store_record_t *record = &state->records[state->active_index];
    char uid[32];
    char tech[22];
    format_uid_hex(record, uid, sizeof(uid));
    snprintf(tech, sizeof(tech), "SAK %02X ATQA %04X", record->sak, record->atqa);
    ui_draw_text(ui, 4, 14, record->name, true);
    ui_draw_text(ui, 4, 26, key_store_type_name(record), true);
    ui_draw_text(ui, 4, 38, uid, true);
    ui_draw_text(ui, 4, 50, tech, true);
}

static void draw_message(keys_app_state_t *state, ui_t *ui)
{
    ui_status_bar_render(ui, "KEYS");
    ui_draw_text_aligned(ui, 0, 23, UI_WIDTH, state->message_title, UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 37, UI_WIDTH, state->message_body, UI_ALIGN_CENTER, true);
    ui_draw_text_aligned(ui, 0, 55, UI_WIDTH, "SELECT: OK", UI_ALIGN_CENTER, true);
}

static void keys_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    keys_app_state_t *state = (keys_app_state_t *)screen->user_data;
    switch (state->view) {
    case KEYS_VIEW_LIST:
        draw_list(state, ui);
        break;
    case KEYS_VIEW_ACTIONS:
        draw_actions(state, ui);
        break;
    case KEYS_VIEW_SCAN:
        draw_scan(state, ui);
        break;
    case KEYS_VIEW_EMULATE:
        draw_emulate(state, ui);
        break;
    case KEYS_VIEW_RENAME:
        draw_rename(state, ui);
        break;
    case KEYS_VIEW_DETAILS:
        draw_details(state, ui);
        break;
    case KEYS_VIEW_MESSAGE:
    default:
        draw_message(state, ui);
        break;
    }
}

static esp_err_t keys_launch(core_context_t *ctx)
{
    memset(&s_keys, 0, sizeof(s_keys));
    s_keys.active_index = (size_t)-1;
    reload_keys(&s_keys);
    s_keys.screen.id = "keys.screen";
    s_keys.screen.title = "KEYS";
    s_keys.screen.user_data = &s_keys;
    s_keys.screen.on_input = keys_on_input;
    s_keys.screen.on_update = keys_on_update;
    s_keys.screen.on_render = keys_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_keys.screen);
}

const core_app_descriptor_t g_keys_app = {
    .id = "keys",
    .name = "Keys",
    .icon = "K",
    .launch = keys_launch,
};
