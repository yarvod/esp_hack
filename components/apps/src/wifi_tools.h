#pragma once
#include "esp_wifi.h"

void wifi_tools_beacon_spam_start(const char** ssids, int count);
void wifi_tools_beacon_spam_stop(void);
bool wifi_tools_beacon_is_running(void);

void wifi_tools_sniffer_start(void);
void wifi_tools_sniffer_stop(void);
bool wifi_tools_sniffer_is_running(void);

void wifi_tools_dns_server_start(void);
void wifi_tools_dns_server_stop(void);

void wifi_tools_ble_spam_start(int type); // 0: AirPods, 1: AppleTV, etc
void wifi_tools_ble_spam_stop(void);
bool wifi_tools_ble_spam_is_running(void);

typedef struct {
    uint32_t beacon;
    uint32_t data;
    uint32_t mgmt;
    uint32_t ctrl;
    uint32_t total;
} sniffer_stats_t;

void wifi_tools_get_sniffer_stats(sniffer_stats_t* stats);
