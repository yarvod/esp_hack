#include "wifi_tools.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "esp_random.h"
#include <string.h>

static bool s_beacon_running = false;
static bool s_sniffer_running = false;
static bool s_dns_running = false;
static bool s_ble_spam_running = false;
static sniffer_stats_t s_stats = {0};

static void beacon_task(void *pv) {
    (void)pv;
    
    // Allocate 32-bit aligned buffer for the WiFi driver
    uint8_t *pkt = malloc(256);
    if (!pkt) { s_beacon_running = false; vTaskDelete(NULL); return; }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 16) ap_count = 16;
    
    wifi_ap_record_t *ap_records = NULL;
    if (ap_count > 0) {
        ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
        if (ap_records) {
            esp_wifi_scan_get_ap_records(&ap_count, ap_records);
        } else {
            ap_count = 0;
        }
    }
    
    char random_ssids[16][32];
    if (ap_count == 0) {
        ap_count = 16;
        for (int i=0; i<16; i++) {
            snprintf(random_ssids[i], sizeof(random_ssids[i]), "WIFI-%04X", (unsigned)(esp_random() & 0xFFFF));
        }
    }
    
    while(s_beacon_running) {
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        wifi_interface_t ifx = (mode == WIFI_MODE_AP) ? WIFI_IF_AP : WIFI_IF_STA;
        
        for(int i=0; i<ap_count; i++) {
            memset(pkt, 0, 256);
            int pos = 0;
            
            pkt[pos++] = 0x80; pkt[pos++] = 0x00; // Frame Control: Beacon
            pkt[pos++] = 0x00; pkt[pos++] = 0x00; // Duration
            for (int j=0; j<6; j++) pkt[pos++] = 0xff; // DA: Broadcast
            pkt[pos++] = 0xDE; pkt[pos++] = 0xAD; pkt[pos++] = 0xBE; pkt[pos++] = 0xEF; pkt[pos++] = 0x00; pkt[pos++] = (uint8_t)i; // SA
            pkt[pos++] = 0xDE; pkt[pos++] = 0xAD; pkt[pos++] = 0xBE; pkt[pos++] = 0xEF; pkt[pos++] = 0x00; pkt[pos++] = (uint8_t)i; // BSSID
            pkt[pos++] = 0x00; pkt[pos++] = 0x00; // Sequence Control
            
            // Fixed Parameters
            for (int j=0; j<8; j++) pkt[pos++] = 0x00; // Timestamp
            pkt[pos++] = 0x64; pkt[pos++] = 0x00; // Beacon Interval
            pkt[pos++] = 0x11; pkt[pos++] = 0x04; // Capability Info
            
            // SSID Tag
            const char *ssid_name = ap_records ? (const char *)ap_records[i].ssid : random_ssids[i];
            int slen = strlen(ssid_name);
            if (slen > 32) slen = 32;
            pkt[pos++] = 0x00; pkt[pos++] = (uint8_t)slen;
            memcpy(pkt + pos, ssid_name, slen); pos += slen;
            
            // Supported Rates
            uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
            memcpy(pkt + pos, rates, sizeof(rates)); pos += sizeof(rates);
            
            // DS Parameter Set (Channel)
            uint8_t chan;
            if (mode == WIFI_MODE_AP) {
                uint8_t prim;
                wifi_second_chan_t sec;
                esp_wifi_get_channel(&prim, &sec);
                chan = prim;
                if (chan == 0) chan = 1;
            } else {
                chan = 1 + (esp_random() % 11);
                esp_wifi_set_channel(chan, WIFI_SECOND_CHAN_NONE);
            }
            pkt[pos++] = 0x03; pkt[pos++] = 0x01; pkt[pos++] = chan;
            
            esp_wifi_80211_tx(ifx, pkt, pos, false);
            
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
    if (ap_records) free(ap_records);
    free(pkt);
    vTaskDelete(NULL);
}

void wifi_tools_beacon_spam_start(const char** ssids, int count) {
    if (s_beacon_running) return;
    s_beacon_running = true;
    xTaskCreate(beacon_task, "beacon_task", 4096, (void*)ssids, 5, NULL);
}
void wifi_tools_beacon_spam_stop(void) { s_beacon_running = false; }
bool wifi_tools_beacon_is_running(void) { return s_beacon_running; }

static void sniffer_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    s_stats.total++;
    if (type == WIFI_PKT_MGMT) s_stats.mgmt++;
    else if (type == WIFI_PKT_DATA) s_stats.data++;
}

void wifi_tools_sniffer_start(void) {
    if (s_sniffer_running) return;
    memset(&s_stats, 0, sizeof(s_stats));
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(sniffer_cb);
    s_sniffer_running = true;
}
void wifi_tools_sniffer_stop(void) { esp_wifi_set_promiscuous(false); s_sniffer_running = false; }
bool wifi_tools_sniffer_is_running(void) { return s_sniffer_running; }
void wifi_tools_get_sniffer_stats(sniffer_stats_t* stats) { memcpy(stats, &s_stats, sizeof(s_stats)); }

static void dns_task(void *pv) {
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(53) };
    bind(s, (struct sockaddr *)&sa, sizeof(sa));
    struct timeval tv = {0, 500000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t d[512];
    while(s_dns_running) {
        struct sockaddr_in ca; socklen_t al = sizeof(ca);
        int len = recvfrom(s, d, 512, 0, (struct sockaddr *)&ca, &al);
        if (len > 12) {
            d[2]|=0x80; d[3]|=0x80; d[7]=1; int p=len;
            d[p++]=0xc0; d[p++]=0x0c; d[p++]=0; d[p++]=1; d[p++]=0; d[p++]=1;
            memset(d+p, 0, 4); p+=4; d[p++]=0; d[p++]=4;
            uint32_t ip = inet_addr("192.168.4.1"); memcpy(d+p, &ip, 4); p+=4;
            sendto(s, d, p, 0, (struct sockaddr *)&ca, al);
        }
    }
    close(s); vTaskDelete(NULL);
}
void wifi_tools_dns_server_start(void) { if(s_dns_running) return; s_dns_running=true; xTaskCreate(dns_task,"dns",4096,NULL,5,NULL); }
void wifi_tools_dns_server_stop(void) { s_dns_running=false; }

static void ble_spam_task(void *pv) {
    esp_log_level_set("NimBLE", ESP_LOG_ERROR);
    uint8_t adv[] = { 
        0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 
        0x20, 0x75, 0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 
        0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 
        0x12, 0x12, 0x12, 0x12, 0x12, 0x12, 0x12 
    };
    struct ble_gap_adv_params ap = { .conn_mode = BLE_GAP_CONN_MODE_NON, .disc_mode = BLE_GAP_DISC_MODE_GEN, .itvl_min=32, .itvl_max=32 };
    
    while(s_ble_spam_running) {
        ble_addr_t rnd; rnd.type = BLE_ADDR_RANDOM; esp_fill_random(rnd.val, 6);
        rnd.val[5] |= 0xC0;
        ble_hs_id_set_rnd(rnd.val);
        ble_gap_adv_set_data(adv, 31);
        ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &ap, NULL, NULL);
        vTaskDelay(pdMS_TO_TICKS(150));
        ble_gap_adv_stop();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}
void wifi_tools_ble_spam_start(int type) { if(s_ble_spam_running) return; s_ble_spam_running=true; xTaskCreate(ble_spam_task,"ble_spam",4096,NULL,5,NULL); }
void wifi_tools_ble_spam_stop(void) { s_ble_spam_running=false; ble_gap_adv_stop(); }
bool wifi_tools_ble_spam_is_running(void) { return s_ble_spam_running; }

