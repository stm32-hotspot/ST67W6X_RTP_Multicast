#ifndef MAIN_APP_H
#define MAIN_APP_H

/**
 *******************************************************************************
 * @file    main_app.h
 * @author  STMicroelectronics
 * @date    2026
 * @brief   Shared application definitions:
 *          - BLE Wi-Fi commissioning
 *          - Wi-Fi scan/connect/disconnect
 *          - shared runtime context for MQTT and RTP modules
 *******************************************************************************
 */

#include <stdint.h>
#include <stdbool.h>

#include "w6x_api.h"

#ifndef REDEFINE_FREERTOS_INTERFACE
#include "app_freertos.h"
#endif /* REDEFINE_FREERTOS_INTERFACE */

#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Shared event flags between modules ----------------------------------------*/

/** Wi-Fi scan done event flag */
#define EVENT_FLAG_WIFI_SCAN_DONE   (1U << 0U)
/** DNS query done event flag */
#define EVENT_FLAG_DNS_DONE         (1U << 1U)
/** Stream active runtime event flag */
#define EVENT_FLAG_STREAM_ACTIVE    (1U << 2U)
/** Stream TX busy runtime event flag */
#define EVENT_FLAG_STREAM_TX_BUSY   (1U << 3U)

/* Shared MQTT buffers -------------------------------------------------------*/

#ifndef MQTT_TOPIC_BUFFER_SIZE
#define MQTT_TOPIC_BUFFER_SIZE      100U
#endif

#ifndef MQTT_MSG_BUFFER_SIZE
#define MQTT_MSG_BUFFER_SIZE        600U
#endif

/**
  * @brief  Shared application runtime context
  */
typedef struct
{
  EventGroupHandle_t app_evt_current;
  EventGroupHandle_t app_runtime_flags;
  EventGroupHandle_t scan_event_flags;
  EventGroupHandle_t dns_event_flags;

  SemaphoreHandle_t mqtt_client_mutex;
  QueueHandle_t sub_msg_queue;

  TaskHandle_t mqtt_task_handle;
  TaskHandle_t mqtt_refresher_task_handle;
  TaskHandle_t sub_task_handle;
  TaskHandle_t stream_task_handle;

  volatile bool mqtt_started;
  volatile bool mqtt_connected;
  volatile TickType_t mqtt_publish_postpone_until;

  bool red_led_status;

  W6X_WiFi_Scan_Opts_t wifi_scan_opts;
  W6X_WiFi_Connect_Opts_t wifi_connect_opts;
  W6X_WiFi_StaStateType_e sta_state;
  W6X_WiFi_Scan_Result_t scan_results;

  /** BLE data buffer to receive message from the ST67W6X Driver */
  uint8_t ble_available_data[247];
  /** Wi-Fi Commissioning update characteristic data */
  uint8_t wifi_comm_update_char_data[247];
  /** Wi-Fi connected SSID */
  uint8_t wifi_connected_ssid[W6X_WIFI_MAX_SSID_SIZE + 1];
  /** Size of Wi-Fi Commissioning update characteristic data */
  uint8_t wifi_comm_update_char_size;

  uint8_t mqtt_sendbuf[2048];
  uint8_t mqtt_recvbuf[1024];
  uint8_t mqtt_topic[MQTT_TOPIC_BUFFER_SIZE];
  uint8_t mqtt_pubmsg[MQTT_MSG_BUFFER_SIZE];

  bool rtp_stream_paused;
} APP_Context_t;

void main_app(void *args);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_APP_H */
