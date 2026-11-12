#ifndef C6_NETWORK_H
#define C6_NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    C6_WIFI_DISCONNECTED,
    C6_WIFI_CONNECTING,
    C6_WIFI_CONNECTED,
    C6_WIFI_RECONNECTING,
    C6_WIFI_UNRESPONSIVE
} c6_wifi_state_t;

void c6_network_init(void);
void c6_network_probe_uart(void);
void c6_network_pre_lcd_handshake(void);
void c6_network_poll(void);
void c6_network_set_enabled(bool enabled);
void c6_network_set_wechat_enabled(bool enabled);
void c6_network_set_cloud_sync_enabled(bool enabled);
void c6_network_request_status(void);
void c6_network_set_latest_ai_summary(int max_heart, int min_heart, int avg_heart,
                                      int max_spo2, int min_spo2, int avg_spo2,
                                      int detect_hour,
                                      bool after_exercise,
                                      const char *local_judgement);
void c6_network_send_ai_question(const char *question);
void c6_network_dismiss_ai_panel(void);
bool c6_network_is_connected(void);

c6_wifi_state_t c6_network_wifi_state(void);
const char *c6_network_wifi_ip(void);
int c6_network_wifi_rssi(void);
void c6_network_bind_wifi_detail_ui(void);

#ifdef __cplusplus
}
#endif

#endif
