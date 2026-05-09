#pragma once
void wifi_mgr_init();
void wifi_mgr_tick();
bool wifi_mgr_connected();
void wifi_mgr_provision(const char* ssid, const char* pwd);
void wifi_mgr_scan(void (*on_result)(const char* json));
