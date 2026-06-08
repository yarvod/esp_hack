#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"
#include "ui/widgets.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "wifi_tools.h"
#include "cJSON.h"
#include <string.h>

#define MAX_RESULTS 16
typedef enum { V_MAIN, V_SCAN, V_WEB, V_SNIF, V_RESL } view_t;
typedef struct { core_screen_t s; view_t v; size_t sel; wifi_ap_record_t ap[MAX_RESULTS]; uint16_t c; bool i; httpd_handle_t srv; } st_t;
static st_t s_w;

static esp_err_t api_st(httpd_req_t *r) {
    cJSON *root = cJSON_CreateObject(); sniffer_stats_t s; wifi_tools_get_sniffer_stats(&s);
    cJSON_AddBoolToObject(root, "beacon", wifi_tools_beacon_is_running());
    cJSON_AddBoolToObject(root, "ble", wifi_tools_ble_spam_is_running());
    cJSON_AddNumberToObject(root, "packets", s.total);
    char *j = cJSON_Print(root); httpd_resp_set_type(r, "application/json");
    httpd_resp_send(r, j, strlen(j)); free(j); cJSON_Delete(root); return ESP_OK;
}
static esp_err_t api_ctrl(httpd_req_t *r) {
    char b[128]; int ret = httpd_req_recv(r, b, 127); if (ret <= 0) return ESP_FAIL; b[ret] = 0;
    cJSON *root = cJSON_Parse(b);
    if (root) {
        cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
        if (cmd) {
            if (!strcmp(cmd->valuestring, "beacon_on")) wifi_tools_beacon_spam_start(NULL, 0);
            else if (!strcmp(cmd->valuestring, "beacon_off")) wifi_tools_beacon_spam_stop();
            else if (!strcmp(cmd->valuestring, "ble_on")) wifi_tools_ble_spam_start(0);
            else if (!strcmp(cmd->valuestring, "ble_off")) wifi_tools_ble_spam_stop();
        }
        cJSON_Delete(root);
    }
    httpd_resp_send(r, "OK", 2); return ESP_OK;
}
static esp_err_t index_get(httpd_req_t *r) {
    const char* h = "<html><head><title>Hacker Console</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>"
                    "body{background:#000;color:#0f0;font-family:monospace;padding:20px; text-align:center;}"
                    ".box{border:2px solid #0f0;padding:20px;margin:20px 0;background:#111;box-shadow:0 0 15px #0f0; border-radius:10px;}"
                    "button{background:#0f0;color:#000;border:none;padding:15px;margin:10px 0;cursor:pointer;font-weight:bold;width:100%;font-size:1.1em;}"
                    ".active{background:#f0f;box-shadow:0 0 10px #f0f; color:#fff;}"
                    "h1{color:#f0f;text-shadow:0 0 10px #f0f;font-size:2em;}"
                    "</style></head><body><h1>ESP32-C6 HACK</h1>"
                    "<div class='box'><h2>DEVICE</h2><p id='pk'>Packets: 0</p><p id='st'>Status: Standby</p></div>"
                    "<div class='box'><h2>WIFI</h2><button id='btn_b' onclick='t(\"beacon\")'>START BEACON SPAM</button></div>"
                    "<div class='box'><h2>BT</h2><button id='btn_bl' onclick='t(\"ble\")'>START BLE SPAM</button></div>"
                    "<script>function t(m){let b=document.getElementById('btn_'+(m=='beacon'?'b':'bl')); let on=b.classList.toggle('active');"
                    "b.innerText=(on?'STOP ':'START ')+m.toUpperCase()+' SPAM';"
                    "fetch('/api/ctrl',{method:'POST',body:JSON.stringify({cmd:m+(on?'_on':'_off')})});}"
                    "setInterval(()=>{fetch('/api/status').then(r=>r.json()).then(j=>{document.getElementById('pk').innerText='Packets: '+j.packets;"
                    "document.getElementById('st').innerText=(j.beacon||j.ble)?'ATTACKING':'IDLE';});},1000);</script></body></html>";
    httpd_resp_send(r, h, strlen(h)); return ESP_OK;
}
static esp_err_t cp_h(httpd_req_t *r) { httpd_resp_set_status(r, "302 Found"); httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/"); httpd_resp_send(r, NULL, 0); return ESP_OK; }

static void s_srv() {
    if (s_w.srv) return;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG(); c.max_uri_handlers = 12; c.lru_purge_enable = true;
    if (httpd_start(&s_w.srv, &c) == ESP_OK) {
        httpd_uri_t u1 = {"/", HTTP_GET, index_get, NULL}; httpd_register_uri_handler(s_w.srv, &u1);
        httpd_uri_t u2 = {"/api/status", HTTP_GET, api_st, NULL}; httpd_register_uri_handler(s_w.srv, &u2);
        httpd_uri_t u3 = {"/api/ctrl", HTTP_POST, api_ctrl, NULL}; httpd_register_uri_handler(s_w.srv, &u3);
        httpd_uri_t u4 = {"/generate_204", HTTP_GET, cp_h, NULL}; httpd_register_uri_handler(s_w.srv, &u4);
        httpd_uri_t u5 = {"/hotspot-detect.html", HTTP_GET, cp_h, NULL}; httpd_register_uri_handler(s_w.srv, &u5);
    }
}
static void w_i(wifi_mode_t m) {
    if (!s_w.i) { nvs_flash_init(); esp_netif_init(); esp_event_loop_create_default(); esp_netif_create_default_wifi_sta(); esp_netif_create_default_wifi_ap(); wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); esp_wifi_init(&cfg); s_w.i = true; }
    esp_wifi_stop(); esp_wifi_set_mode(m); esp_wifi_start();
}
static void w_up(core_context_t *ctx, core_screen_t *sc, uint32_t dt) {
    if (s_w.v == V_SCAN) { uint16_t ac=0; if (esp_wifi_scan_get_ap_num(&ac)==ESP_OK && ac>0) { s_w.c=(ac>MAX_RESULTS)?MAX_RESULTS:ac; esp_wifi_scan_get_ap_records(&s_w.c, s_w.ap); s_w.v=V_RESL; s_w.sel=0; core_nav_mark_dirty(&ctx->nav); } }
}
static bool w_in(core_context_t *ctx, core_screen_t *sc, const core_input_event_t *e) {
    if (e->phase != CORE_INPUT_PHASE_PRESS) return false;
    if (e->action == CORE_INPUT_BACK) { if(s_w.v != V_MAIN) { wifi_tools_sniffer_stop(); wifi_tools_dns_server_stop(); s_w.v = V_MAIN; s_w.sel = 0; core_nav_mark_dirty(&ctx->nav); return true; } return false; }
    int c = (s_w.v == V_MAIN) ? 4 : (s_w.v == V_RESL) ? s_w.c : 0;
    if (e->action == CORE_INPUT_UP && c > 0) { s_w.sel = (s_w.sel + c - 1) % c; core_nav_mark_dirty(&ctx->nav); return true; }
    if (e->action == CORE_INPUT_DOWN && c > 0) { s_w.sel = (s_w.sel + 1) % c; core_nav_mark_dirty(&ctx->nav); return true; }
    if (e->action == CORE_INPUT_SELECT) {
        if (s_w.v == V_MAIN) {
            if (s_w.sel == 0) { w_i(WIFI_MODE_STA); s_w.v = V_SCAN; wifi_scan_config_t sc={0}; sc.show_hidden=true; sc.scan_type=WIFI_SCAN_TYPE_ACTIVE; esp_wifi_scan_start(&sc, false); }
            else if (s_w.sel == 1) { w_i(WIFI_MODE_AP); wifi_config_t ac={.ap={.ssid="esp32c6_hack",.password="12345678",.authmode=WIFI_AUTH_WPA2_PSK,.max_connection=4}}; esp_wifi_set_config(WIFI_IF_AP, &ac); wifi_tools_dns_server_start(); s_srv(); s_w.v = V_WEB; }
            else if (s_w.sel == 2) { w_i(WIFI_MODE_STA); wifi_tools_sniffer_start(); s_w.v = V_SNIF; }
            else if (s_w.sel == 3) {
                if(wifi_tools_beacon_is_running()) {
                    wifi_tools_beacon_spam_stop();
                } else {
                    w_i(WIFI_MODE_STA);
                    esp_wifi_set_promiscuous(true);
                    wifi_tools_beacon_spam_start(NULL, 0);
                }
            }
        }
        core_nav_mark_dirty(&ctx->nav); return true;
    }
    return false;
}
static void w_r(core_context_t *ctx, core_screen_t *sc, ui_t *ui) {
    ui_status_bar_render(ui, "WIFI TOOLS");
    if (s_w.v == V_MAIN) {
        const char *it[] = {"SCANNER", "WEB CONSOLE", "SNIFFER", "BEACON SPAM"};
        for (int i=0; i<4; i++) { 
            int y=18+i*11; 
            if(i==s_w.sel) ui_fill_rect(ui,2,y-2,124,11,true); 
            ui_draw_text(ui,5,y,it[i],i!=s_w.sel); 
            if(i==3 && wifi_tools_beacon_is_running()) ui_draw_text(ui, 90, y, "RUN", i!=s_w.sel);
        }
    } else if (s_w.v == V_SCAN) { ui_draw_text(ui, 5, 30, "SCANNING...", true);
    } else if (s_w.v == V_WEB) { ui_draw_text(ui, 5, 20, "SSID: esp32c6_hack", true); ui_draw_text(ui, 5, 32, "PASS: 12345678", true); ui_draw_text(ui, 5, 44, "IP: 192.168.4.1", true); ui_draw_text(ui, 5, 56, "PORTAL: ACTIVE", true);
    } else if (s_w.v == V_SNIF) { sniffer_stats_t s; wifi_tools_get_sniffer_stats(&s); char b[32]; snprintf(b,32,"Total: %lu",s.total); ui_draw_text(ui, 5, 30, b, true); snprintf(b,32,"Mgmt: %lu",s.mgmt); ui_draw_text(ui, 5, 42, b, true); core_nav_mark_dirty(&ctx->nav);
    } else if (s_w.v == V_RESL) {
        for (int i=0; i<4; i++) { int idx=(s_w.sel/4)*4+i; if(idx>=s_w.c) break; int y=15+i*10; if(idx==s_w.sel) ui_fill_rect(ui,2,y-1,124,9,true); char b[32]; snprintf(b,32,"%-12.12s %d", (char*)s_w.ap[idx].ssid, s_w.ap[idx].rssi); ui_draw_text(ui,5,y,b,idx!=s_w.sel); }
    }
}
static esp_err_t wifi_launch(core_context_t *ctx) { memset(&s_w,0,sizeof(s_w)); s_w.s.id="wifi"; s_w.s.user_data=&s_w; s_w.s.on_input=w_in; s_w.s.on_update=w_up; s_w.s.on_render=w_r; return core_nav_push(ctx, &ctx->nav, &s_w.s); }
const core_app_descriptor_t g_wifi_app = { .id="wifi", .name="WiFi", .icon="W", .launch=wifi_launch };
