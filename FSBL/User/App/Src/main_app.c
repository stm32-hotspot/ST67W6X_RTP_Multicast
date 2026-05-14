/**
 *******************************************************************************
 * @file    main_app.c
 * @author  STMicroelectronics
 * @date    2026
 * @brief   Main application:
 *          - BLE Wi-Fi commissioning
 *          - Wi-Fi scan/connect/disconnect
 *          - MQTT telemetry + subscription
 *          - H264 RTP streaming over Wi-Fi
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "app_config.h"
#include "common_parser.h"
#include "logging.h"
#include "logshell_ctrl.h"
#include "main.h"
#include "main_app.h"
#include "ble_comm_app.h"
#include "mqtt_app.h"
#include "rtp_stream_app.h"
#include "shell.h"
#include "spi_iface.h"
#include "w6x_version.h"
#include "w6x_api.h"

#if ST67_ARCH == W6X_ARCH_T02
#include "lwip.h"
#include "lwip/sockets.h"
#include <lwip/api.h>
#include <lwip/errno.h>
#include <netdb.h>
#include "dns.h"
#endif /* ST67_ARCH */

#ifndef REDEFINE_FREERTOS_INTERFACE
#include "app_freertos.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#endif /* REDEFINE_FREERTOS_INTERFACE */

/* External RTC handle if available */
extern RTC_HandleTypeDef hrtc;

/* Private tunables ----------------------------------------------------------*/

/* App selection */
#define APP_LWIP_TEST               0U
#define APP_RTP_TEST                1U
#define APP_LOSS_DUMMY_TEST         2U
#define APP_LOSS_H264_TEST          3U
#define APP_H264_UDP_STREAM         4U
#define APP_H264_RTP_STREAM         5U

/* Wi-Fi / BLE / app events */
#define WIFI_DTIM                   1U      /*!< DTIM period */

/* Application events */
/** Event when user button is pressed */
#define EVT_APP_BUTTON                              (1U << 0U)
/** Event when Wi-Fi is connected to an Access Point */
#define EVT_APP_WIFI_CONNECTED                      (1U << 21U)
/** Event when Wi-Fi connection timeout */
#define EVT_APP_WIFI_CONNECTION_TIMEOUT             (1U << 22U)

/** Application events bitmask */
#define EVT_APP_ALL_BIT         (EVT_APP_BLE_CONNECTED  | \
                                 EVT_APP_BLE_DISCONNECTED | \
                                 EVT_APP_BLE_CONNECTION_PARAM_UPDATE | \
                                 EVT_APP_BLE_READ | \
                                 EVT_APP_BLE_WRITE | \
                                 EVT_APP_BLE_SERVICE_FOUND | \
                                 EVT_APP_BLE_CHAR_FOUND | \
                                 EVT_APP_BLE_INDICATION_STATUS_ENABLED | \
                                 EVT_APP_BLE_INDICATION_STATUS_DISABLED | \
                                 EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED | \
                                 EVT_APP_BLE_NOTIFICATION_STATUS_DISABLED | \
                                 EVT_APP_BLE_NOTIFICATION_DATA | \
                                 EVT_APP_BLE_MTU_SIZE | \
                                 EVT_APP_BLE_PAIRING_FAILED | \
                                 EVT_APP_BLE_PAIRING_COMPLETED | \
                                 EVT_APP_BLE_PAIRING_CONFIRM | \
                                 EVT_APP_BLE_PASSKEY_ENTRY | \
                                 EVT_APP_BLE_PASSKEY_DISPLAYED | \
                                 EVT_APP_BLE_PASSKEY_CONFIRM | \
                                 EVT_APP_BLE_PAIRING_CANCELED | \
                                 EVT_APP_WIFI_CONNECTED | \
                                 EVT_APP_WIFI_CONNECTION_TIMEOUT )

/* Private types -------------------------------------------------------------*/

/**
  * @brief  Application information structure
  */
typedef struct
{
  char *version;                            /*!< Version of the application */
  char *name;                               /*!< Name of the application */
} APP_Info_t;

/* Private data --------------------------------------------------------------*/

/** Shared application context */
static APP_Context_t g_app_ctx = {0};

/** Deferred button processing flags
  * NOTE:
  * Do not call FreeRTOS APIs from USER_BUTTON EXTI callback.
  * Just latch a software flag and handle the action in task context.
  */
static volatile uint8_t g_user_button_pending = 0U;
static volatile uint32_t g_user_button_last_ms = 0U;

/** Application information */
static const APP_Info_t app_info =
{
  .name = "STM32N6 + ST67W6X demo with:\n"
          "- RTP Video Streaming\n"
          "- Wi-Fi Commissioning over BLE\n"
          "- MQTT over TLS\n",
  .version = W6X_VERSION_STR
};

/* Private function prototypes -----------------------------------------------*/

static void APP_error_cb(W6X_Status_t ret_w6x, char const *func_name);
static void APP_setevent(EventGroupHandle_t *app_event, uint32_t evt);
static void APP_mqtt_cb(W6X_event_id_t event_id, void *event_args);
static void APP_net_cb(W6X_event_id_t event_id, void *event_args);
static void APP_wifi_cb(W6X_event_id_t event_id, void *event_args);
static void APP_EnsureDnsServers(void);
static void APP_ProcessPendingButton(void);

extern void     EWLPoolChoiceCb(uint8_t **pool, size_t *size);
extern void     EWLPoolReleaseCb(uint8_t **pool);
extern void     HAL_GPIO_EXTI_Rising_Callback(uint16_t pin);
extern void     HAL_GPIO_EXTI_Falling_Callback(uint16_t pin);
extern int32_t  w6x_socket_udp_create(uint8_t bind, uint16_t port);

/* Public API definitions ----------------------------------------------------*/

/**
  * @brief  Main application task.
  * @param  args: Task argument, unused.
  * @retval None
  * @note   Initializes application resources, registers ST67W6X callbacks,
  *         starts Wi-Fi, LWIP and BLE subsystems, then processes BLE and Wi-Fi
  *         events in a task loop.
  */
void main_app(void *args)
{
  W6X_WiFi_Connect_t conn_data = {0};
  int32_t status = 0;
  EventBits_t eventBits = 0;

  (void)args;

  memset(&g_app_ctx, 0, sizeof(g_app_ctx));

  g_app_ctx.app_evt_current = xEventGroupCreate();
  if (g_app_ctx.app_evt_current == NULL)
  {
    return;
  }

  g_app_ctx.app_runtime_flags = xEventGroupCreate();
  if (g_app_ctx.app_runtime_flags == NULL)
  {
    return;
  }

  g_app_ctx.mqtt_client_mutex = xSemaphoreCreateMutex();
  if (g_app_ctx.mqtt_client_mutex == NULL)
  {
    return;
  }

  /* Initialize the logging utilities */
  LoggingInit();

  LogInfo("\n\n#########################################\n");
  LogInfo("Welcome to %s", app_info.name);
  LogInfo("#########################################\n\n");

  LogInfo("Build: %s %s\n", __TIME__, __DATE__);
  LogInfo("--------------- Host info ---------------\n");
  LogInfo("Host FW Version:          %s\n", app_info.version);

  /* Register the application callback to received events from ST67W6X Driver */
  W6X_App_Cb_t App_cb = {0};
  App_cb.APP_wifi_cb  = APP_wifi_cb;
  App_cb.APP_net_cb   = APP_net_cb;
  App_cb.APP_ble_cb   = BLE_COMM_APP_HandleBleEvent;
  App_cb.APP_mqtt_cb  = APP_mqtt_cb;
  App_cb.APP_error_cb = APP_error_cb;
  W6X_RegisterAppCb(&App_cb);

  /* Initialize ST67W6X */
  status = W6X_Init();
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to initialize ST67W6X Driver, %" PRIi32 "\n", status);
    goto _err;
  }

  status = W6X_WiFi_Init();
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to initialize ST67W6X Wifi component, %" PRIi32 "\n", status);
    goto _err;
  }
  LogInfo("Wifi init ready!\n");

  status = MX_LWIP_Init();
  if (status != 0)
  {
    LogError("Failed to initialize LWIP stack, %" PRIi32 "\n", status);
    goto _err;
  }

  status = BLE_COMM_APP_Init(&g_app_ctx, g_app_ctx.app_evt_current);
  if (status != W6X_STATUS_OK)
  {
    goto _err;
  }

  while (1)
  {
    /* Wait to receive BLE/Wi-Fi events.
     * Use a finite timeout so deferred USER_BUTTON processing is always serviced.
     */
    eventBits = xEventGroupWaitBits(g_app_ctx.app_evt_current,
                                    EVT_APP_ALL_BIT,
                                    pdTRUE,
                                    pdFALSE,
                                    pdMS_TO_TICKS(50U));

    /* Process deferred USER_BUTTON action in task context */
    APP_ProcessPendingButton();

    /* Process BLE notification */
    if ((eventBits & EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED) != 0U)
    {
      BLE_COMM_APP_ProcessNotificationEnabled();
    }

    /* Process BLE read */
    if ((eventBits & EVT_APP_BLE_READ) != 0U)
    {
    }

    /* Process BLE write */
    if ((eventBits & EVT_APP_BLE_WRITE) != 0U)
    {
      BLE_COMM_APP_ProcessWrite();
    }

    if ((eventBits & EVT_APP_WIFI_CONNECTED) != 0U)
    {
      if (W6X_WiFi_Station_GetState(&g_app_ctx.sta_state, &conn_data) != W6X_STATUS_OK)
      {
        LogError("Failed to retrieve Wi-Fi station state\n");
        continue;
      }

      if (g_app_ctx.sta_state != W6X_WIFI_STATE_STA_CONNECTED)
      {
        LogWarn("Wi-Fi connected event received but state is not connected\n");
        continue;
      }

      LogInfo("Connected to following Access Point :\n");
      LogInfo("[" MACSTR "] Channel: %" PRIu32 " | RSSI: %" PRIi32 " | SSID: %s\n",
              MAC2STR(conn_data.MAC),
              conn_data.Channel,
              conn_data.Rssi,
              conn_data.SSID);

      BLE_COMM_APP_NotifyWifiConnected(&conn_data);

      APP_EnsureDnsServers();

      vTaskDelay(pdMS_TO_TICKS(500U));
      APP_MQTT_Start(&g_app_ctx);
      APP_RTP_Stream_Start(&g_app_ctx);
    }

    if ((eventBits & EVT_APP_WIFI_CONNECTION_TIMEOUT) != 0U)
    {
      BLE_COMM_APP_NotifyWifiConnectionTimeout();
    }

    if ((eventBits & EVT_APP_BLE_PASSKEY_ENTRY) != 0U)
    {
      /* Process BLE passkey entry */
      BLE_COMM_APP_ProcessPasskeyEntry();
    }

    if ((eventBits & EVT_APP_BLE_PASSKEY_CONFIRM) != 0U)
    {
      /* Process BLE passkey confirm */
      BLE_COMM_APP_ProcessPasskeyConfirm();
    }

    if ((eventBits & EVT_APP_BLE_PAIRING_CONFIRM) != 0U)
    {
      /* Process BLE pairing confirm */
      BLE_COMM_APP_ProcessPairingConfirm();
    }

    if ((eventBits & EVT_APP_BLE_PAIRING_COMPLETED) != 0U)
    {
      /* Process BLE pairing complete */
      BLE_COMM_APP_ProcessPairingCompleted();
    }

    /* Process BLE disconnection complete */
    if ((eventBits & EVT_APP_BLE_DISCONNECTED) != 0U)
    {
      /* Re-launch advertising after disconnection */
      LogInfo("Re-start Advertising\n");
      status = W6X_Ble_AdvStart();
      if (status != W6X_STATUS_OK)
      {
    	  LogError("Failed to start advertising, %" PRIi32 "\n", status);
      }
    }
  }

_err:
  /* De-initialize the ST67W6X Driver */
  W6X_WiFi_DeInit();
  LogInfo("\nGoing IDLE!\n");
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

/* Private function definitions ----------------------------------------------*/

/**
  * @brief  Processes a deferred USER_BUTTON press in task context.
  * @param  None
  * @retval None
  * @note   If the RTP stream is active and Wi-Fi is connected, toggles stream
  *         pause/resume. Otherwise forwards the button action to the BLE
  *         commissioning application.
  */
static void APP_ProcessPendingButton(void)
{
  uint8_t pending = 0U;

  taskENTER_CRITICAL();
  pending = g_user_button_pending;
  g_user_button_pending = 0U;
  taskEXIT_CRITICAL();

  if (pending == 0U)
  {
    return;
  }

  LogInfo("USER_BUTTON pressed\n");

  if ((g_app_ctx.stream_task_handle != NULL) &&
      (g_app_ctx.sta_state == W6X_WIFI_STATE_STA_CONNECTED))
  {
    APP_RTP_Stream_TogglePause(&g_app_ctx);
  }
  else
  {
    BLE_COMM_APP_ProcessButton();
  }
}

/**
  * @brief  Application error callback for ST67W6X driver operations.
  * @param  ret_w6x: Driver status code.
  * @param  func_name: Name of the function that reported the error.
  * @retval None
  */
static void APP_error_cb(W6X_Status_t ret_w6x, char const *func_name)
{
  LogError("[%s] in %s API\n", W6X_StatusToStr(ret_w6x), func_name);
}

/**
  * @brief  Application MQTT event callback.
  * @param  event_id: MQTT-related event identifier.
  * @param  event_args: Optional event-specific arguments, unused.
  * @retval None
  * @note   Currently logs the received event identifier for debug purposes.
  */
static void APP_mqtt_cb(W6X_event_id_t event_id, void *event_args)
{
  (void)event_args;
  LogDebug("APP_mqtt_cb event_id=%" PRIu32 "\n", (uint32_t)event_id);
}

/**
  * @brief  Application network event callback.
  * @param  event_id: Network-related event identifier.
  * @param  event_args: Optional event-specific arguments.
  * @retval None
  * @note   Placeholder callback for future network event handling.
  */
static void APP_net_cb(W6X_event_id_t event_id, void *event_args)
{
  (void)event_id;
  (void)event_args;
}

/**
  * @brief  Application Wi-Fi event callback.
  * @param  event_id: Wi-Fi event identifier.
  * @param  event_args: Optional event-specific arguments.
  * @retval None
  * @note   Updates application state and posts events based on Wi-Fi connect,
  *         disconnect, and reason notifications.
  */
static void APP_wifi_cb(W6X_event_id_t event_id, void *event_args)
{
  switch (event_id)
  {
    case W6X_WIFI_EVT_CONNECTED_ID:
      APP_setevent(&g_app_ctx.app_evt_current, EVT_APP_WIFI_CONNECTED);
      break;

    case W6X_WIFI_EVT_DISCONNECTED_ID:
      memset(g_app_ctx.wifi_connect_opts.SSID, 0, W6X_WIFI_MAX_SSID_SIZE + 1U);
      memset(g_app_ctx.wifi_connect_opts.Password, 0, W6X_WIFI_MAX_PASSWORD_SIZE + 1U);
      LogInfo("Station disconnected from Access Point\n");
      memset(g_app_ctx.wifi_connected_ssid, 0, sizeof(g_app_ctx.wifi_connected_ssid));
      g_app_ctx.mqtt_connected = false;
      break;

    case W6X_WIFI_EVT_REASON_ID:
      memset(g_app_ctx.wifi_connect_opts.SSID, 0, W6X_WIFI_MAX_SSID_SIZE + 1U);
      memset(g_app_ctx.wifi_connect_opts.Password, 0, W6X_WIFI_MAX_PASSWORD_SIZE + 1U);
      LogInfo("Reason: %s\n", W6X_WiFi_ReasonToStr(event_args));
      break;

    default:
      break;
  }
}

/**
  * @brief  Sets an application event bit from task or ISR context.
  * @param  app_event: Pointer to the target event group handle.
  * @param  evt: Event bit mask to set.
  * @retval None
  * @note   Automatically selects the ISR-safe or task-context FreeRTOS API
  *         depending on the current execution context.
  */
static void APP_setevent(EventGroupHandle_t *app_event, uint32_t evt)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if ((app_event == NULL) || (*app_event == NULL))
  {
    return;
  }

  if (xPortIsInsideInterrupt())
  {
    xEventGroupSetBitsFromISR(*app_event, evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken != pdFALSE)
    {
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
  else
  {
    xEventGroupSetBits(*app_event, evt);
  }
}

/**
  * @brief  Ensures valid DNS servers are configured in the LWIP stack.
  * @param  None
  * @retval None
  * @note   If DNS server slots are empty, assigns public fallback servers and
  *         logs the resulting DNS configuration.
  */
static void APP_EnsureDnsServers(void)
{
  ip_addr_t dns_addr;
  const ip_addr_t *dns0 = dns_getserver(0);
  const ip_addr_t *dns1 = dns_getserver(1);

  if ((dns0 == NULL) || ip_addr_isany_val(*dns0))
  {
    IP4_ADDR(ip_2_ip4(&dns_addr), 8, 8, 8, 8);
    dns_setserver(0, &dns_addr);
    LogWarn("DNS server[0] was empty, set to %s\n", ipaddr_ntoa(&dns_addr));
  }

  if ((dns1 == NULL) || ip_addr_isany_val(*dns1))
  {
    IP4_ADDR(ip_2_ip4(&dns_addr), 1, 1, 1, 1);
    dns_setserver(1, &dns_addr);
    LogWarn("DNS server[1] was empty, set to %s\n", ipaddr_ntoa(&dns_addr));
  }

  LogInfo("DNS server[0]: %s\n", ipaddr_ntoa(dns_getserver(0)));
  LogInfo("DNS server[1]: %s\n", ipaddr_ntoa(dns_getserver(1)));
}

/**
  * @brief  Callback used by the encoder wrapper to select a memory pool.
  * @param  pool: Pointer to the pool pointer to fill.
  * @param  size: Pointer to the pool size to fill.
  * @retval None
  * @note   Currently implemented as a placeholder.
  */
void EWLPoolChoiceCb(uint8_t **pool, size_t *size)
{
  (void)pool;
  (void)size;
}

/**
  * @brief  Callback used by the encoder wrapper to release a memory pool.
  * @param  pool: Pointer to the pool pointer to release.
  * @retval None
  * @note   Currently implemented as a placeholder.
  */
void EWLPoolReleaseCb(uint8_t **pool)
{
  (void)pool;
}

/**
  * @brief  EXTI rising-edge callback handler.
  * @param  pin: GPIO pin that triggered the interrupt.
  * @retval None
  * @note   Handles SPI ready signaling and latches USER_BUTTON presses using
  *         a simple debounce mechanism without calling FreeRTOS APIs from ISR.
  */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
  if (pin == SPI_RDY_Pin)
  {
    spi_on_txn_data_ready();
  }

  if (pin == USER_BUTTON_Pin)
  {
	HAL_GPIO_TogglePin(LED_RED_GPIO_Port, LED_RED_Pin);
    uint32_t now = HAL_GetTick();

    /* Simple debounce without any FreeRTOS API in ISR */
    if ((now - g_user_button_last_ms) >= 200U)
    {
      g_user_button_last_ms = now;
      g_user_button_pending = 1U;
    }
  }
}

/**
  * @brief  EXTI falling-edge callback handler.
  * @param  pin: GPIO pin that triggered the interrupt.
  * @retval None
  * @note   Handles SPI header acknowledge signaling.
  */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
  if (pin == SPI_RDY_Pin)
  {
    spi_on_header_ack();
  }
}
