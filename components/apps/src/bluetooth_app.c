#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "wifi_tools.h"
#include <string.h>

#define MAX_BLE 16
typedef enum { BT_MAIN, BT_SCAN, BT_SPAM } bt_v_t;
typedef struct { char name[24]; int rssi; ble_addr_t addr; } ble_dev_t;
typedef struct { core_screen_t screen; bt_v_t view; size_t selected; ble_dev_t devices[MAX_BLE]; uint16_t dev_count; bool bt_init; } bt_app_st_t;
static bt_app_st_t s_bt;

static int bt_on_scan(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        for (int i=0; i<s_bt.dev_count; i++) {
            if (memcmp(&s_bt.devices[i].addr, &event->disc.addr, 6) == 0) {
                s_bt.devices[i].rssi = event->disc.rssi; return 0;
            }
        }
        if (s_bt.dev_count < MAX_BLE) {
            ble_dev_t *d = &s_bt.devices[s_bt.dev_count++];
            d->rssi = event->disc.rssi; d->addr = event->disc.addr;
            struct ble_hs_adv_fields fields;
            ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (fields.name) {
                int len = (fields.name_len > 23) ? 23 : fields.name_len;
                memcpy(d->name, fields.name, len); d->name[len] = 0;
            } else {
                snprintf(d->name, 24, "%02X%02X%02X", d->addr.val[5], d->addr.val[4], d->addr.val[3]);
            }
        }
    }
    return 0;
}
static void host_task(void *param) { nimble_port_run(); }
static void bt_init_stack(void) {
    if (s_bt.bt_init) return;
    esp_log_level_set("NimBLE", ESP_LOG_ERROR);
    nvs_flash_init();
    nimble_port_init();
    nimble_port_freertos_init(host_task);
    s_bt.bt_init = true;
}

static bool bt_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event) {
    if (event->phase != CORE_INPUT_PHASE_PRESS) return false;
    if (event->action == CORE_INPUT_BACK) {
        if (s_bt.view != BT_MAIN) {
            ble_gap_disc_cancel(); wifi_tools_ble_spam_stop();
            s_bt.view = BT_MAIN; s_bt.selected = 0;
            core_nav_mark_dirty(&ctx->nav); return true;
        }
        return false;
    }
    int c = (s_bt.view == BT_MAIN) ? 2 : (s_bt.view == BT_SCAN) ? s_bt.dev_count : 0;
    if (event->action == CORE_INPUT_UP && c > 0) { s_bt.selected = (s_bt.selected + c - 1) % c; core_nav_mark_dirty(&ctx->nav); return true; }
    if (event->action == CORE_INPUT_DOWN && c > 0) { s_bt.selected = (s_bt.selected + 1) % c; core_nav_mark_dirty(&ctx->nav); return true; }
    if (event->action == CORE_INPUT_SELECT && s_bt.view == BT_MAIN) {
        if (s_bt.selected == 0) {
            bt_init_stack(); s_bt.view = BT_SCAN; s_bt.dev_count = 0;
            struct ble_gap_disc_params dp = { .filter_duplicates = 1, .passive = 1 };
            ble_gap_disc(0, BLE_HS_FOREVER, &dp, bt_on_scan, NULL);
        } else if (s_bt.selected == 1) {
            bt_init_stack(); wifi_tools_ble_spam_start(0); s_bt.view = BT_SPAM;
        }
        core_nav_mark_dirty(&ctx->nav); return true;
    }
    return false;
}
static void bt_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui) {
    ui_status_bar_render(ui, "BT TOOLS");
    if (s_bt.view == BT_MAIN) {
        const char *it[] = {"SCANNER", "BLE SPAM"};
        for (int i=0; i<2; i++) {
            int y = 22 + i * 12;
            if (i == s_bt.selected) ui_fill_rect(ui, 2, y - 2, 124, 11, true);
            ui_draw_text(ui, 5, y, it[i], i != s_bt.selected);
        }
    } else if (s_bt.view == BT_SCAN && s_bt.dev_count == 0) {
        ui_draw_text(ui, 5, 30, "SCANNING...", true);
    } else if (s_bt.view == BT_SPAM) {
        ui_draw_text(ui, 5, 25, "SPAM: AIRPODS", true);
        ui_draw_text(ui, 5, 37, "STATUS: ACTIVE", true);
    } else if (s_bt.view == BT_SCAN) {
        for (int i=0; i<4; i++) {
            int idx = (s_bt.selected / 4) * 4 + i;
            if (idx >= s_bt.dev_count) break;
            int y = 15 + i * 10;
            if (idx == s_bt.selected) ui_fill_rect(ui, 2, y - 1, 124, 9, true);
            char b[32]; snprintf(b, 32, "%-12.12s %d", s_bt.devices[idx].name, s_bt.devices[idx].rssi);
            ui_draw_text(ui, 5, y, b, idx != s_bt.selected);
        }
    }
}
static esp_err_t bluetooth_launch(core_context_t *ctx) {
    memset(&s_bt, 0, sizeof(s_bt));
    s_bt.screen.id = "bt.screen"; s_bt.screen.user_data = &s_bt;
    s_bt.screen.on_input = bt_on_input; s_bt.screen.on_render = bt_on_render;
    return core_nav_push(ctx, &ctx->nav, &s_bt.screen);
}
const core_app_descriptor_t g_bluetooth_app = { .id = "bluetooth", .name = "Bluetooth", .icon = "B", .launch = bluetooth_launch };
