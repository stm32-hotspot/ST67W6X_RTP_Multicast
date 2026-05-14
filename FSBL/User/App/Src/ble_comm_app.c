/**
 *******************************************************************************
 * @file    ble_comm_app.c
 * @author  STMicroelectronics
 * @date    2026
 * @brief   BLE Wi-Fi commissioning application helper:
 *          - BLE init / advertising / GATT commissioning service
 *          - BLE write handling for Wi-Fi credentials and control
 *          - BLE notifications for scan results and monitoring
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "app_config.h"
#include "logging.h"
#include "main.h"
#include "main_app.h"
#include "ble_comm_app.h"
#include "w6x_version.h"
#include "w6x_api.h"

#ifndef REDEFINE_FREERTOS_INTERFACE
#include "app_freertos.h"
#include "event_groups.h"
#include "task.h"
#endif /* REDEFINE_FREERTOS_INTERFACE */

/* Private tunables ----------------------------------------------------------*/

/* Wi-Fi / BLE / app events */
#define WIFI_SCAN_TIMEOUT                           10000U  /*!< Delay before to declare the scan in failure */

/* Application events mirrored from main_app.c when needed internally */
/** Event when Wi-Fi connection timeout */
#define BLE_COMM_APP_EVT_WIFI_CONNECTION_TIMEOUT    (1U << 22U)

/** BLE Wi-Fi Commissioning Service index */
#define WIFI_COMMISSIONING_SERVICE_INDEX         0U
/** BLE Service index */
#define WIFI_CONTROL_CHAR_INDEX                  0U
/** BLE Wi-Fi Control Characteristic index */
#define WIFI_CONFIGURE_CHAR_INDEX                1U
/** BLE Wi-Fi Configure Characteristic index */
#define WIFI_AP_LIST_CHAR_INDEX                  2U
/** BLE Wi-Fi Monitoring Characteristic index */
#define WIFI_MONITORING_CHAR_INDEX               3U

/*
 The following 128bits UUIDs have been generated from the random UUID
 generator:
 0000ff9acc7a482a984a7f2ed5b3e58f: Service 128bits UUID
 0000fe9b8e2245419d4c21edae82ed19: Characteristic 128bits UUID
 0000fe9c8e2245419d4c21edae82ed19: Characteristic 128bits UUID
 0000fe9d8e2245419d4c21edae82ed19: Characteristic 128bits UUID
 0000fe9e8e2245419d4c21edae82ed19: Characteristic 128bits UUID
 */
#define WIFI_COMMISSIONING_SERVICE_UUID          "0000ff9acc7a482a984a7f2ed5b3e58f"
#define WIFI_CONTROL_CHAR_UUID                   "0000fe9b8e2245419d4c21edae82ed19"
#define WIFI_CONFIGURE_CHAR_UUID                 "0000fe9c8e2245419d4c21edae82ed19"
#define WIFI_AP_LIST_CHAR_UUID                   "0000fe9d8e2245419d4c21edae82ed19"
#define WIFI_MONITORING_CHAR_UUID                "0000fe9e8e2245419d4c21edae82ed19"

/* Wi-Fi Control Characteristic first byte possible values */
#define CONTROL_ACTION_START_SCAN                0x1U  /*!< Start Wi-Fi scan */
#define CONTROL_ACTION_CONNECT                   0x3U  /*!< Connect to Wi-Fi */
#define CONTROL_ACTION_DISCONNECT                0x4U  /*!< Disconnect from Wi-Fi */
#define CONTROL_ACTION_PING                      0x5U  /*!< Ping Wi-Fi */

/* Wi-Fi Configure Characteristic first byte possible values */
#define CONFIGURE_TYPE_SSID                      0x1U  /*!< Configure SSID */
#define CONFIGURE_TYPE_PWD                       0x2U  /*!< Configure Password */
#define CONFIGURE_TYPE_SECURITY_FLAG             0x5U  /*!< Configure Security Flag */

/* Wi-Fi Monitoring Characteristic first byte possible values */
#define MONITORING_TYPE_CONNECTION_DONE          0x4U  /*!< Connection done */
#define MONITORING_TYPE_ERROR                    0x6U  /*!< Error */

/* Wi-Fi Monitoring Characteristic second byte possible values */
#define MONITORING_DATA_CONNECTION_TIMEOUT       0x1U  /*!< Connection timeout */

/** Generic Passkey */
#define BLE_GENERIC_PASSKEY                      123456U

#ifndef BLE_ADDR_FMT
#define BLE_ADDR_FMT "%02X:%02X:%02X:%02X:%02X:%02X"
#endif /* BLE_ADDR_FMT */

#ifndef BLE_ADDR_ARG
#define BLE_ADDR_ARG(a) \
  (unsigned int)(a)[0], \
  (unsigned int)(a)[1], \
  (unsigned int)(a)[2], \
  (unsigned int)(a)[3], \
  (unsigned int)(a)[4], \
  (unsigned int)(a)[5]
#endif /* BLE_ADDR_ARG */

/* Private types -------------------------------------------------------------*/

/**
  * @brief  BLE parameters structure
  */
typedef struct
{
  uint8_t service_idx;                      /*!< Service index */
  uint8_t charac_idx;                       /*!< Characteristic index */
  uint8_t notification_status[2];           /*!< Notification status */
  uint8_t indication_status[2];             /*!< Indication status */
  uint16_t mtu_size;                        /*!< MTU Size */
  uint32_t PassKey;                         /*!< BLE Security passkey */
  uint32_t available_data_length;           /*!< Length of the available data */
  W6X_Ble_Device_t remote_ble_device;       /*!< BLE Remote device */
} BLE_COMM_APP_Ble_Data_t;

/**
  * @brief  BLE characteristic information structure
  */
typedef struct
{
  uint8_t service_index;                    /*!< Service index */
  uint8_t char_index;                       /*!< Characteristic index */
  const char *char_uuid;                    /*!< Service UUID */
  uint8_t uuid_type;                        /*!< UUID type */
  uint8_t char_property;                    /*!< Characteristic property */
  uint8_t char_permission;                  /*!< Characteristic permission */
  const char *desc;                         /*!< Characteristic description */
} BLE_COMM_APP_Ble_Char_t;

/* Private data --------------------------------------------------------------*/

/** Advertising Data */
static char a_AdvData[36] =
{
  '0', 'F', /* Manuf data length */
  'F', 'F', /* Manuf data Flag */
  '3', '0', '0', '0', /*  */
  '0', '2', /* Blue ST SDK v2  */
  '9', 'A', /* Board ID */
  'F', 'E', /* FW ID */
  '0', '0', /* FW data */
  '0', '0', /* FW data */
  '0', '0', /* FW data */
  '0', '0', /* BD Address MSB */
  '0', '0', /*  */
  '0', '0', /*  */
  '0', '0', /*  */
  '0', '0', /*  */
  '0', '0', /* BD Address LSB */
};

/** Shared application context */
static APP_Context_t *g_ble_comm_app_ctx = NULL;

/** BLE event group */
static EventGroupHandle_t g_ble_comm_app_evt = NULL;

/** BLE parameters */
static BLE_COMM_APP_Ble_Data_t app_ble_params =
{
  /* Initialize remote device struct */
  .remote_ble_device.RSSI = 0,
  .remote_ble_device.IsConnected = 0,
  .remote_ble_device.conn_handle = 0,
  .remote_ble_device.DeviceName = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  .remote_ble_device.BDAddr = {0, 0, 0, 0, 0, 0},
  .service_idx = 0,                /* Initialize service index to 0 */
  .charac_idx = 0,                 /* Initialize characteristic index to 0 */
  .notification_status = {0, 0},   /* Initialize notification status array to {0, 0} */
  .indication_status = {0, 0},     /* Initialize indication status array to {0, 0} */
  .mtu_size = 0,                   /* Initialize MTU size to 0 */
  .available_data_length = 0       /* Initialize available data length to 0 */
};

/** BLE characteristics */
static const BLE_COMM_APP_Ble_Char_t app_ble_char[] =
{
  {
    .service_index = WIFI_COMMISSIONING_SERVICE_INDEX,
    .char_index = WIFI_CONTROL_CHAR_INDEX,
    .char_uuid = WIFI_CONTROL_CHAR_UUID,
    .uuid_type = W6X_BLE_UUID_TYPE_128,
    .char_property = W6X_BLE_CHAR_PROP_WRITE_WITH_RESP,
    .char_permission = W6X_BLE_CHAR_PERM_WRITE,
    .desc = "BLE service"
  },
  {
    .service_index = WIFI_COMMISSIONING_SERVICE_INDEX,
    .char_index = WIFI_CONFIGURE_CHAR_INDEX,
    .char_uuid = WIFI_CONFIGURE_CHAR_UUID,
    .uuid_type = W6X_BLE_UUID_TYPE_128,
    .char_property = W6X_BLE_CHAR_PROP_WRITE_WITH_RESP,
    .char_permission = W6X_BLE_CHAR_PERM_WRITE,
    .desc = "BLE WIFI Control charac"
  },
  {
    .service_index = WIFI_COMMISSIONING_SERVICE_INDEX,
    .char_index = WIFI_AP_LIST_CHAR_INDEX,
    .char_uuid = WIFI_AP_LIST_CHAR_UUID,
    .uuid_type = W6X_BLE_UUID_TYPE_128,
    .char_property = W6X_BLE_CHAR_PROP_NOTIFY,
    .char_permission = W6X_BLE_CHAR_PERM_READ,
    .desc = "BLE WIFI Configure charac"
  },
  {
    .service_index = WIFI_COMMISSIONING_SERVICE_INDEX,
    .char_index = WIFI_MONITORING_CHAR_INDEX,
    .char_uuid = WIFI_MONITORING_CHAR_UUID,
    .uuid_type = W6X_BLE_UUID_TYPE_128,
    .char_property = W6X_BLE_CHAR_PROP_READ | W6X_BLE_CHAR_PROP_NOTIFY,
    .char_permission = W6X_BLE_CHAR_PERM_READ | W6X_BLE_CHAR_PERM_WRITE,
    .desc = "BLE WIFI Monitoring charac"
  }
};

/** List of bonded BLE devices */
static W6X_Ble_Bonded_Devices_Result_t app_ble_BondedDeviceList = {0};

/* Private function prototypes -----------------------------------------------*/

static void BLE_COMM_APP_setevent(uint32_t evt);
static void BLE_COMM_APP_WifiScanCb(int32_t status, W6X_WiFi_Scan_Result_t *scan_results);
static uint8_t BLE_COMM_APP_BuildApScanPayload(uint8_t *dst,
                                               size_t dst_size,
                                               const uint8_t *ssid,
                                               uint8_t channel,
                                               int16_t rssi,
                                               uint32_t security);

static void BLE_COMM_APP_SendWifiScanReportNotification(void);
static void BLE_COMM_APP_SendWifiMonitoringNotification(void);

/* Public API definitions ----------------------------------------------------*/

/**
  * @brief  Initialize the BLE commissioning application.
  * @param  app_ctx Pointer to the shared application context used by the BLE
  *                 commissioning module.
  * @param  app_evt_current Event group used to notify the main application task
  *                         of BLE-related events.
  * @retval W6X_STATUS_OK on success, otherwise a negative or driver-specific
  *         error code.
  * @note   This function:
  *         - initializes the BLE stack in server mode,
  *         - configures the device name and advertising payload,
  *         - creates the BLE commissioning service and characteristics,
  *         - enables BLE security parameters,
  *         - starts advertising.
  */
int32_t BLE_COMM_APP_Init(APP_Context_t *app_ctx, EventGroupHandle_t app_evt_current)
{
  int32_t status = W6X_STATUS_OK;
  int32_t data_index = 0;
  uint8_t bd_address[W6X_BLE_BD_ADDR_SIZE] = {0};
  const char hex_chars[] = "0123456789ABCDEF";
  char ble_device_name[W6X_BLE_DEVICE_NAME_SIZE] = {0};

  if ((app_ctx == NULL) || (app_evt_current == NULL))
  {
    LogError("BLE_COMM_APP_Init invalid parameter\n");
    return -1;
  }

  g_ble_comm_app_ctx = app_ctx;
  g_ble_comm_app_evt = app_evt_current;

  /* Initialize the ST67W6X BLE module */
  status = W6X_Ble_Init(W6X_BLE_MODE_SERVER,
                        g_ble_comm_app_ctx->ble_available_data,
                        sizeof(g_ble_comm_app_ctx->ble_available_data));
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to initialize ST67W6X BLE component, %" PRIi32 "\n", status);
    return status;
  }

  LogInfo("Ble init is done\n");

  status = W6X_Ble_GetBDAddress(bd_address);
  if (status == W6X_STATUS_OK)
  {
    LogInfo("BD Address: " BLE_ADDR_FMT "\n", BLE_ADDR_ARG(bd_address));
  }
  else
  {
    LogError("BD Address identification failed, %" PRIi32 "\n", status);
    return status;
  }

  (void)snprintf(ble_device_name, sizeof(ble_device_name), W6X_BLE_HOSTNAME "_%02X", bd_address[5]);
  status = W6X_Ble_SetDeviceName(ble_device_name);
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to set device name, %" PRIi32 "\n", status);
    return status;
  }

  /* Configure the ST67W6X BLE module
   * - Set the Tx power to 0 (minimal)
   * - Set the Bluetooth Device Address
   * - Set the Advertising data
   */
  LogInfo("Configure BLE\n");
  status = W6X_Ble_SetTxPower(0);
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to set TX power, %" PRIi32 "\n", status);
    return status;
  }

  /* Fill adv data with BD Address */
  while (data_index < 6)
  {
    a_AdvData[20 + (2 * data_index)] = hex_chars[(bd_address[data_index] & 0xF0U) >> 4];
    a_AdvData[20 + (2 * data_index) + 1] = hex_chars[bd_address[data_index] & 0x0FU];
    data_index++;
  }

  status = W6X_Ble_SetAdvData((const char *)a_AdvData);
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to set ADV data, %" PRIi32 "\n", status);
    return status;
  }
  LogInfo("BLE configuration is done\n");

  /* Create the BLE Commissioning Service */
  LogInfo("\nBLE Commissioning Service Creation\n");
  status = W6X_Ble_CreateService(WIFI_COMMISSIONING_SERVICE_INDEX,
                                 WIFI_COMMISSIONING_SERVICE_UUID,
                                 W6X_BLE_UUID_TYPE_128);
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to Create service, %" PRIi32 "\n", status);
    return status;
  }

  /* Create the BLE Characteristics */
  for (uint8_t i = 0; i < (uint8_t)(sizeof(app_ble_char) / sizeof(BLE_COMM_APP_Ble_Char_t)); i++)
  {
    status = W6X_Ble_CreateCharacteristic(app_ble_char[i].service_index,
                                          app_ble_char[i].char_index,
                                          app_ble_char[i].char_uuid,
                                          app_ble_char[i].uuid_type,
                                          app_ble_char[i].char_property,
                                          app_ble_char[i].char_permission);
    if (status != W6X_STATUS_OK)
    {
      LogError("Failed to create %s, %" PRIi32 "\n", app_ble_char[i].desc, status);
      return status;
    }
    LogInfo("- %s created\n", app_ble_char[i].desc);
  }

  /* Register the BLE Characteristics */
  status = W6X_Ble_RegisterCharacteristics();
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to Register characteristics, %" PRIi32 "\n", status);
    return status;
  }
  LogInfo("- BLE services and charac registered\n");

  /* Setup security mode */
  status = W6X_Ble_SetSecurityParam(W6X_BLE_SEC_IO_KEYBOARD_DISPLAY);
  if (status != W6X_STATUS_OK)
  {
    LogError("BLE Set Security Parameters failed, %" PRIi32 "\n", status);
  }

  /* Start the BLE Advertising mode: the ST67W6X can be detected as BLE device */
  LogInfo("\nStart BLE advertising\n");
  status = W6X_Ble_AdvStart();
  if (status != W6X_STATUS_OK)
  {
    LogError("Failed to start advertising, %" PRIi32 "\n", status);
    return status;
  }

  LogInfo("\nBLE advertising is started. To commission WiFi:\n");
  LogInfo("- go to: https://applible.github.io/Web_Bluetooth_App_ST67/\n");
  LogInfo("- or use the ST BLE Toolbox App on iOS/Android\n");

  return W6X_STATUS_OK;
}

/**
  * @brief  Handle asynchronous BLE events received from the BLE stack.
  * @param  event_id Identifier of the BLE event to process.
  * @param  event_args Pointer to the event-specific callback data provided by
  *                    the BLE stack.
  * @retval None
  * @note   This function updates the internal BLE context, decodes write
  *         operations related to Wi-Fi commissioning, and forwards significant
  *         events to the main application through the event group.
  */
void BLE_COMM_APP_HandleBleEvent(W6X_event_id_t event_id, void *event_args)
{
  W6X_Ble_CbParamData_t *p_param_ble_data = (W6X_Ble_CbParamData_t *)event_args;

  if (p_param_ble_data == NULL)
  {
    LogWarn("BLE callback received NULL event_args for event_id=%" PRIu32 "\n", (uint32_t)event_id);
    return;
  }

  switch (event_id)
  {
    case W6X_BLE_EVT_CONNECTED_ID:
      LogInfo(" -> BLE CONNECTED [Conn_Handle: %" PRIu16 "]\n", p_param_ble_data->remote_ble_device.conn_handle);
      /* Fill remote device structure */
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.remote_ble_device.IsConnected = p_param_ble_data->remote_ble_device.IsConnected;
      memcpy(app_ble_params.remote_ble_device.BDAddr,
             p_param_ble_data->remote_ble_device.BDAddr,
             sizeof(app_ble_params.remote_ble_device.BDAddr));
      memcpy(app_ble_params.remote_ble_device.DeviceName,
             p_param_ble_data->remote_ble_device.DeviceName,
             sizeof(app_ble_params.remote_ble_device.DeviceName));
      memcpy(app_ble_params.remote_ble_device.ManufacturerData,
             p_param_ble_data->remote_ble_device.ManufacturerData,
             sizeof(app_ble_params.remote_ble_device.ManufacturerData));

      BLE_COMM_APP_setevent(EVT_APP_BLE_CONNECTED);
      break;

    case W6X_BLE_EVT_CONNECTION_PARAM_ID:
      LogInfo(" -> BLE CONNECTION PARAM UPDATE [Conn_Handle: %" PRIu16 "]\n",
              p_param_ble_data->remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_CONNECTION_PARAM_UPDATE);
      break;

    case W6X_BLE_EVT_DISCONNECTED_ID:
      LogInfo(" -> BLE DISCONNECTED [Conn_Handle: %" PRIu16 "]\n",
              p_param_ble_data->remote_ble_device.conn_handle);
      /* Reinitialize remote device struct */
      app_ble_params.remote_ble_device.RSSI = 0;
      app_ble_params.remote_ble_device.IsConnected = 0;
      app_ble_params.remote_ble_device.conn_handle = 0xFFU;
      memset(app_ble_params.remote_ble_device.DeviceName, 0, sizeof(app_ble_params.remote_ble_device.DeviceName));
      memset(app_ble_params.remote_ble_device.BDAddr, 0, sizeof(app_ble_params.remote_ble_device.BDAddr));
      memset(app_ble_params.remote_ble_device.ManufacturerData, 0, sizeof(app_ble_params.remote_ble_device.ManufacturerData));

      BLE_COMM_APP_setevent(EVT_APP_BLE_DISCONNECTED);
      break;

    case W6X_BLE_EVT_INDICATION_STATUS_ENABLED_ID:
      LogInfo(" -> BLE INDICATION ENABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      BLE_COMM_APP_setevent(EVT_APP_BLE_INDICATION_STATUS_ENABLED);
      break;

    case W6X_BLE_EVT_INDICATION_STATUS_DISABLED_ID:
      LogInfo(" -> BLE INDICATION DISABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      BLE_COMM_APP_setevent(EVT_APP_BLE_INDICATION_STATUS_DISABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_STATUS_ENABLED_ID:
      LogInfo(" -> BLE NOTIFICATION ENABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      BLE_COMM_APP_setevent(EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_STATUS_DISABLED_ID:
      LogInfo(" -> BLE NOTIFICATION DISABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      BLE_COMM_APP_setevent(EVT_APP_BLE_NOTIFICATION_STATUS_DISABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_DATA_ID:
      LogInfo(" -> BLE NOTIFICATION DATA.\n");
      BLE_COMM_APP_setevent(EVT_APP_BLE_NOTIFICATION_DATA);
      break;

    case W6X_BLE_EVT_WRITE_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.service_idx = p_param_ble_data->service_idx;
      app_ble_params.charac_idx = p_param_ble_data->charac_idx;
      app_ble_params.available_data_length = p_param_ble_data->available_data_length;

      LogInfo(" -> BLE WRITE [Conn_Handle: %" PRIu16 ", Service: %" PRIu16 ", Charac: %" PRIu16
              ", length %" PRIu32 "]\n",
              p_param_ble_data->remote_ble_device.conn_handle,
              p_param_ble_data->service_idx,
              p_param_ble_data->charac_idx,
              p_param_ble_data->available_data_length);

      for (uint32_t i = 0; i < p_param_ble_data->available_data_length; i++)
      {
        LogInfo("0x%02X\n", g_ble_comm_app_ctx->ble_available_data[i]);
      }

      /* Manage Wi-Fi configuration */
      if ((app_ble_params.service_idx == WIFI_COMMISSIONING_SERVICE_INDEX) &&
          (app_ble_params.charac_idx == WIFI_CONFIGURE_CHAR_INDEX))
      {
        LogInfo("WIFI Configure Charac\n");

        if (app_ble_params.available_data_length < 1U)
        {
          LogError("Invalid config payload length\n");
          break;
        }

        if (g_ble_comm_app_ctx->ble_available_data[0] == CONFIGURE_TYPE_SSID)
        {
          size_t copy_len = 0U;

          if (app_ble_params.available_data_length <= 1U)
          {
            LogError("SSID payload is empty\n");
            break;
          }

          memset(g_ble_comm_app_ctx->wifi_connect_opts.SSID, 0, W6X_WIFI_MAX_SSID_SIZE + 1U);

          copy_len = app_ble_params.available_data_length - 1U;
          if (copy_len > W6X_WIFI_MAX_SSID_SIZE)
          {
            copy_len = W6X_WIFI_MAX_SSID_SIZE;
          }

          memcpy(g_ble_comm_app_ctx->wifi_connect_opts.SSID,
                 &g_ble_comm_app_ctx->ble_available_data[1],
                 copy_len);
          g_ble_comm_app_ctx->wifi_connect_opts.SSID[copy_len] = '\0';

          LogInfo("SSID configured: %s\n", g_ble_comm_app_ctx->wifi_connect_opts.SSID);
        }
        else if (g_ble_comm_app_ctx->ble_available_data[0] == CONFIGURE_TYPE_PWD)
        {
          size_t copy_len = 0U;

          if (app_ble_params.available_data_length <= 1U)
          {
            LogError("Password payload is empty\n");
            break;
          }

          memset(g_ble_comm_app_ctx->wifi_connect_opts.Password, 0, W6X_WIFI_MAX_PASSWORD_SIZE + 1U);

          copy_len = app_ble_params.available_data_length - 1U;
          if (copy_len > W6X_WIFI_MAX_PASSWORD_SIZE)
          {
            copy_len = W6X_WIFI_MAX_PASSWORD_SIZE;
          }

          memcpy(g_ble_comm_app_ctx->wifi_connect_opts.Password,
                 &g_ble_comm_app_ctx->ble_available_data[1],
                 copy_len);
          g_ble_comm_app_ctx->wifi_connect_opts.Password[copy_len] = '\0';

          LogInfo("Password configured\n");
        }
        else if (g_ble_comm_app_ctx->ble_available_data[0] == CONFIGURE_TYPE_SECURITY_FLAG)
        {
          LogInfo("Security flag configuration received - not used in this integration\n");
        }
      }

      BLE_COMM_APP_setevent(EVT_APP_BLE_WRITE);
      break;

    case W6X_BLE_EVT_READ_ID:
      LogInfo(" -> BLE READ.\n");
      BLE_COMM_APP_setevent(EVT_APP_BLE_READ);
      break;

    case W6X_BLE_EVT_PASSKEY_ENTRY_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      LogInfo(" -> BLE PassKey Entry [Conn_Handle %" PRIu16 "]\n",
              app_ble_params.remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PASSKEY_ENTRY);
      break;

    case W6X_BLE_EVT_PASSKEY_CONFIRM_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.PassKey = p_param_ble_data->PassKey;
      LogInfo(" -> BLE PassKey received = %06" PRIu32 " [Conn_Handle %" PRIu16 "]\n",
              app_ble_params.PassKey,
              app_ble_params.remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PASSKEY_CONFIRM);
      break;

    case W6X_BLE_EVT_PAIRING_CONFIRM_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      LogInfo(" -> BLE Pairing Confirm [Conn_Handle %" PRIu16 "]\n",
              app_ble_params.remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PAIRING_CONFIRM);
      break;

    case W6X_BLE_EVT_PAIRING_COMPLETED_ID:
      LogInfo(" -> BLE Pairing Complete\n");
      BLE_COMM_APP_setevent(EVT_APP_BLE_PAIRING_COMPLETED);
      break;

    case W6X_BLE_EVT_PASSKEY_DISPLAY_ID:
      app_ble_params.PassKey = p_param_ble_data->PassKey;
      LogInfo(" -> BLE PASSKEY  = %06" PRIu32 "\n", app_ble_params.PassKey);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PASSKEY_DISPLAYED);
      break;

    case W6X_BLE_EVT_PAIRING_FAILED_ID:
      LogInfo(" -> BLE Pairing Failed [Conn_Handle: %" PRIu16 "]\n",
              app_ble_params.remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PAIRING_FAILED);
      break;

    case W6X_BLE_EVT_PAIRING_CANCELED_ID:
      LogInfo(" -> BLE Pairing Canceled [Conn_Handle %" PRIu16 "]\n",
              app_ble_params.remote_ble_device.conn_handle);
      BLE_COMM_APP_setevent(EVT_APP_BLE_PAIRING_CANCELED);
      break;

    default:
      break;
  }
}

/**
  * @brief  Process user button action related to BLE security or connection.
  * @retval None
  * @note   If a BLE peer is connected, this function disconnects it.
  *         Otherwise, it iterates through the bonded device database and
  *         unpairs all stored devices.
  */
void BLE_COMM_APP_ProcessButton(void)
{
  int32_t status = W6X_STATUS_OK;

  if (g_ble_comm_app_ctx == NULL)
  {
    return;
  }

  if (app_ble_params.remote_ble_device.IsConnected != 0x00U)
  {
    status = W6X_Ble_Disconnect(app_ble_params.remote_ble_device.conn_handle);
    if (status != W6X_STATUS_OK)
    {
      LogError("BLE disconnection failed, %" PRIi32 "\n", status);
    }
  }
  else
  {
    LogInfo("Unpair bonded devices\n");
    W6X_Ble_SecurityGetBondedDeviceList(&app_ble_BondedDeviceList);
    while (app_ble_BondedDeviceList.Count != 0U)
    {
      uint32_t count = app_ble_BondedDeviceList.Count - 1U;

      LogInfo("Bonded Device: " BLE_ADDR_FMT "\n",
              BLE_ADDR_ARG(app_ble_BondedDeviceList.Bonded_device[count].BDAddr));

      status = W6X_Ble_SecurityUnpair(app_ble_BondedDeviceList.Bonded_device[count].BDAddr,
                                      W6X_BLE_PUBLIC_ADDR);
      if (status != W6X_STATUS_OK)
      {
        LogError("failed to unpair bonded devices, %" PRIi32 "\n", status);
      }

      LogInfo(BLE_ADDR_FMT " unpaired\n",
              BLE_ADDR_ARG(app_ble_BondedDeviceList.Bonded_device[count].BDAddr));

      W6X_Ble_SecurityGetBondedDeviceList(&app_ble_BondedDeviceList);
    }

    LogInfo("No more bonded device\n");
  }
}

/**
  * @brief  Send Wi-Fi monitoring information when BLE notifications become enabled.
  * @retval None
  * @note   If the device is already connected to a Wi-Fi network, this function
  *         immediately sends a monitoring notification containing the connected
  *         SSID so that the BLE client can synchronize its state.
  */
void BLE_COMM_APP_ProcessNotificationEnabled(void)
{
  if (g_ble_comm_app_ctx == NULL)
  {
    return;
  }

  if (g_ble_comm_app_ctx->wifi_connected_ssid[0] != 0U)
  {
    uint8_t monitoring_data[W6X_WIFI_MAX_SSID_SIZE + 2U] = {0};
    size_t ssid_len = strnlen((char *)g_ble_comm_app_ctx->wifi_connected_ssid, W6X_WIFI_MAX_SSID_SIZE);

    monitoring_data[0] = MONITORING_TYPE_CONNECTION_DONE;
    memcpy(&monitoring_data[1], g_ble_comm_app_ctx->wifi_connected_ssid, ssid_len);
    monitoring_data[1U + ssid_len] = '\0';

    /* If receiver expects C-string style SSID including '\0' */
    g_ble_comm_app_ctx->wifi_comm_update_char_size = (uint8_t)(1U + ssid_len + 1U);

    memcpy(g_ble_comm_app_ctx->wifi_comm_update_char_data,
           monitoring_data,
           g_ble_comm_app_ctx->wifi_comm_update_char_size);

    BLE_COMM_APP_SendWifiMonitoringNotification();
  }
}

/**
  * @brief  Process a BLE write targeting the Wi-Fi control characteristic.
  * @retval None
  * @note   Supported operations are:
  *         - start Wi-Fi scan,
  *         - connect to a configured access point,
  *         - disconnect from Wi-Fi,
  *         - ping request placeholder.
  *         This function uses the data previously captured in the BLE write
  *         event callback.
  */
void BLE_COMM_APP_ProcessWrite(void)
{
  int32_t status = W6X_STATUS_OK;

  if (g_ble_comm_app_ctx == NULL)
  {
    return;
  }

  if ((app_ble_params.service_idx == WIFI_COMMISSIONING_SERVICE_INDEX) &&
      (app_ble_params.charac_idx == WIFI_CONTROL_CHAR_INDEX))
  {
    LogInfo("WIFI Control Charac\n");

    if (app_ble_params.available_data_length < 1U)
    {
      LogError("Invalid BLE control payload length\n");
      return;
    }

    if (g_ble_comm_app_ctx->ble_available_data[0] == CONTROL_ACTION_START_SCAN) /* Start Scan */
    {
      LogInfo("WIFI Scan Enable\n");
      /* Run a Wi-Fi scan to retrieve the list of all nearby Access Points */
      g_ble_comm_app_ctx->scan_event_flags = xEventGroupCreate();
      if (g_ble_comm_app_ctx->scan_event_flags == NULL)
      {
        LogError("Failed to create scan event group\n");
        return;
      }

      status = W6X_WiFi_Scan(&g_ble_comm_app_ctx->wifi_scan_opts, &BLE_COMM_APP_WifiScanCb);
      if (status != W6X_STATUS_OK)
      {
        LogError("Failed to start scan, %" PRIi32 "\n", status);
        vEventGroupDelete(g_ble_comm_app_ctx->scan_event_flags);
        g_ble_comm_app_ctx->scan_event_flags = NULL;
        return;
      }

      /* Wait to receive the EVENT_FLAG_SCAN_DONE event. The scan is declared as failed after 'ScanTimeout' delay */
      EventBits_t scanBits = xEventGroupWaitBits(g_ble_comm_app_ctx->scan_event_flags,
                                                 EVENT_FLAG_WIFI_SCAN_DONE,
                                                 pdTRUE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT));

      vEventGroupDelete(g_ble_comm_app_ctx->scan_event_flags);
      g_ble_comm_app_ctx->scan_event_flags = NULL;

      if ((scanBits & EVENT_FLAG_WIFI_SCAN_DONE) == 0U)
      {
        LogError("Scan Failed\n");
        return;
      }

      /* Retrieve the configuration of nearby Access Point */
      for (uint32_t ap_index = 0; ap_index < g_ble_comm_app_ctx->scan_results.Count; ap_index++)
      {
        size_t ssid_len = strnlen((char *)g_ble_comm_app_ctx->scan_results.AP[ap_index].SSID,
                                  W6X_WIFI_MAX_SSID_SIZE);

        if (ssid_len > 0U)
        {
          g_ble_comm_app_ctx->wifi_comm_update_char_size =
            BLE_COMM_APP_BuildApScanPayload(g_ble_comm_app_ctx->wifi_comm_update_char_data,
                                            sizeof(g_ble_comm_app_ctx->wifi_comm_update_char_data),
                                            g_ble_comm_app_ctx->scan_results.AP[ap_index].SSID,
                                            g_ble_comm_app_ctx->scan_results.AP[ap_index].Channel,
                                            g_ble_comm_app_ctx->scan_results.AP[ap_index].RSSI,
                                            g_ble_comm_app_ctx->scan_results.AP[ap_index].Security);

          if (g_ble_comm_app_ctx->wifi_comm_update_char_size == 0U)
          {
            LogError("Failed to build BLE AP scan payload for SSID: %.*s\n",
                     (int)ssid_len,
                     (char *)g_ble_comm_app_ctx->scan_results.AP[ap_index].SSID);
            continue;
          }

          /* Send the configuration of each Access Point as BLE notification */
          BLE_COMM_APP_SendWifiScanReportNotification();
        }
      }
    }
    else if (g_ble_comm_app_ctx->ble_available_data[0] == CONTROL_ACTION_CONNECT) /* Connect */
    {
      /* Connect the device to the selected Access Point */
      LogInfo("WIFI Connect\n");
      status = W6X_WiFi_Connect(&g_ble_comm_app_ctx->wifi_connect_opts);
      if (status != W6X_STATUS_OK)
      {
        LogError("failed to connect, %" PRIi32 "\n", status);
        BLE_COMM_APP_setevent(BLE_COMM_APP_EVT_WIFI_CONNECTION_TIMEOUT);
      }
      else
      {
        LogInfo("Wi-Fi connection initiated\n");
      }
    }
    else if (g_ble_comm_app_ctx->ble_available_data[0] == CONTROL_ACTION_DISCONNECT) /* Disconnect */
    {
      /* Disconnect the device from the Access Point */
      LogInfo("WIFI Disconnect\n");
      status = W6X_WiFi_Disconnect(1);
      if (status == W6X_STATUS_OK)
      {
        LogInfo("Disconnect success\n");
      }
      else
      {
        LogError("Disconnect failed, %" PRIi32 "\n", status);
      }
    }
    else if (g_ble_comm_app_ctx->ble_available_data[0] == CONTROL_ACTION_PING) /* Ping */
    {
      LogInfo("WIFI Ping request received - not implemented in this integration\n");
    }
    else
    {
      LogWarn("Unknown WIFI control opcode: 0x%02X\n", g_ble_comm_app_ctx->ble_available_data[0]);
    }
  }
}

/**
  * @brief  Process BLE passkey entry request.
  * @retval None
  * @note   This implementation always provides a generic fixed passkey to the
  *         BLE stack when a valid connection handle is available.
  */
void BLE_COMM_APP_ProcessPasskeyEntry(void)
{
  int32_t status = W6X_STATUS_OK;

  /* Process BLE passkey entry */
  app_ble_params.PassKey = BLE_GENERIC_PASSKEY;
  if (app_ble_params.remote_ble_device.conn_handle != 0xFFU)
  {
    status = W6X_Ble_SecuritySetPassKey(app_ble_params.remote_ble_device.conn_handle, app_ble_params.PassKey);
    if (status != W6X_STATUS_OK)
    {
      LogError("Failed to set passkey, %" PRIi32 "\nPress button while disconnected to clear security Database and relaunch pairing process\n",
               status);
    }
    else
    {
      LogInfo("Set passkey\n");
    }
  }
  else
  {
    /* Connection handle not correctly identified */
    LogInfo("Press button while disconnected to clear security Database and relaunch pairing process\n");
  }
}

/**
  * @brief  Confirm the BLE passkey during the pairing procedure.
  * @retval None
  * @note   A valid connection handle is required. If unavailable, the function
  *         only logs guidance for clearing the security database.
  */
void BLE_COMM_APP_ProcessPasskeyConfirm(void)
{
  int32_t status = W6X_STATUS_OK;

  /* Process BLE passkey confirm */
  if (app_ble_params.remote_ble_device.conn_handle != 0xFFU)
  {
    status = W6X_Ble_SecurityPassKeyConfirm(app_ble_params.remote_ble_device.conn_handle);
    if (status != W6X_STATUS_OK)
    {
      LogError("Failed to send passkey confirm, %" PRIi32 "\nPress button while disconnected to clear security Database and relaunch pairing process\n",
               status);
    }
    else
    {
      LogInfo("Sent passkey confirm\n");
    }
  }
  else
  {
    /* Connection handle not correctly identified */
    LogInfo("Press button while disconnected to clear security Database and relaunch pairing process\n");
  }
}

/**
  * @brief  Confirm the BLE pairing procedure.
  * @retval None
  * @note   This function acknowledges the pairing request to the BLE stack
  *         using the stored connection handle.
  */
void BLE_COMM_APP_ProcessPairingConfirm(void)
{
  int32_t status = W6X_STATUS_OK;

  /* Process BLE pairing confirm */
  if (app_ble_params.remote_ble_device.conn_handle != 0xFFU)
  {
    status = W6X_Ble_SecurityPairingConfirm(app_ble_params.remote_ble_device.conn_handle);
    if (status != W6X_STATUS_OK)
    {
      LogError("Pairing Confirm Failed, %" PRIi32 "\nPress button while disconnected to clear security Database and relaunch pairing process\n",
               status);
    }
    else
    {
      LogInfo("Pairing Confirm\n");
    }
  }
  else
  {
    /* Connection handle not correctly identified:
     * random address used for connection different from address registered in security Database */
    LogInfo("Press button while disconnected to clear security Database and relaunch pairing process\n");
  }
}

/**
  * @brief  Process end of BLE pairing sequence.
  * @retval None
  * @note   Current implementation only logs that pairing has completed
  *         successfully.
  */
void BLE_COMM_APP_ProcessPairingCompleted(void)
{
  /* Process BLE pairing complete */
  LogInfo("Pairing Completed\n");
}

/**
  * @brief  Notify the BLE client that Wi-Fi connection has been established.
  * @param  conn_data Pointer to the Wi-Fi connection information structure.
  * @retval None
  * @note   The notification payload contains a monitoring type byte followed by
  *         the connected SSID as a null-terminated string.
  */
void BLE_COMM_APP_NotifyWifiConnected(const W6X_WiFi_Connect_t *conn_data)
{
  if ((g_ble_comm_app_ctx == NULL) || (conn_data == NULL))
  {
    return;
  }

  {
    size_t connected_ssid_len = strnlen((char *)conn_data->SSID, W6X_WIFI_MAX_SSID_SIZE);

    memset(g_ble_comm_app_ctx->wifi_connected_ssid, 0, sizeof(g_ble_comm_app_ctx->wifi_connected_ssid));
    memcpy(g_ble_comm_app_ctx->wifi_connected_ssid, conn_data->SSID, connected_ssid_len);
  }

  /* Send notification when Wi-Fi connected */
  {
    uint8_t monitoring_data[W6X_WIFI_MAX_SSID_SIZE + 2U] = {0};
    size_t ssid_len = strnlen((char *)conn_data->SSID, W6X_WIFI_MAX_SSID_SIZE);

    /* Add Connection Done Flag at the beginning of the payload */
    monitoring_data[0] = MONITORING_TYPE_CONNECTION_DONE;
    memcpy(&monitoring_data[1], conn_data->SSID, ssid_len);
    monitoring_data[1U + ssid_len] = '\0';

    /* Payload length = Flag Length + SSID length + terminating '\0' */
    g_ble_comm_app_ctx->wifi_comm_update_char_size = (uint8_t)(1U + ssid_len + 1U);

    /* Copy SSID of connected device */
    memcpy(g_ble_comm_app_ctx->wifi_comm_update_char_data,
           monitoring_data,
           g_ble_comm_app_ctx->wifi_comm_update_char_size);

    BLE_COMM_APP_SendWifiMonitoringNotification();
  }
}

/**
  * @brief  Notify the BLE client that Wi-Fi connection has timed out.
  * @retval None
  * @note   The notification payload contains an error monitoring type followed
  *         by the timeout error code.
  */
void BLE_COMM_APP_NotifyWifiConnectionTimeout(void)
{
  if (g_ble_comm_app_ctx == NULL)
  {
    return;
  }

  /* Send notification if Wi-Fi connection error */
  {
    uint8_t monitoring_data[2] = {MONITORING_TYPE_ERROR, MONITORING_DATA_CONNECTION_TIMEOUT};

    g_ble_comm_app_ctx->wifi_comm_update_char_size = 2U;
    memcpy(g_ble_comm_app_ctx->wifi_comm_update_char_data,
           (void *)&monitoring_data,
           g_ble_comm_app_ctx->wifi_comm_update_char_size);

    BLE_COMM_APP_SendWifiMonitoringNotification();
  }
}

/* Private function definitions ----------------------------------------------*/

/**
  * @brief  Set an application event bit in the BLE event group.
  * @param  evt Event bit mask to set.
  * @retval None
  * @note   This helper transparently supports both task and ISR contexts.
  *         When called from an interrupt, it uses the FreeRTOS ISR-safe API.
  */
static void BLE_COMM_APP_setevent(uint32_t evt)
{
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (g_ble_comm_app_evt == NULL)
  {
    return;
  }

  if (xPortIsInsideInterrupt())
  {
    xEventGroupSetBitsFromISR(g_ble_comm_app_evt, evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken != pdFALSE)
    {
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
  else
  {
    xEventGroupSetBits(g_ble_comm_app_evt, evt);
  }
}

/**
  * @brief  Callback invoked when the asynchronous Wi-Fi scan completes.
  * @param  status Scan status returned by the Wi-Fi stack.
  * @param  scan_results Pointer to the scan result structure.
  * @retval None
  * @note   The callback stores the scan results in the shared application
  *         context and signals the waiting task through the scan event group.
  *         The current implementation does not use the status parameter
  *         directly.
  */
static void BLE_COMM_APP_WifiScanCb(int32_t status, W6X_WiFi_Scan_Result_t *scan_results)
{
  (void)status;

  if (g_ble_comm_app_ctx == NULL)
  {
    return;
  }

  LogInfo("WiFi Scan done.\n");
  if (scan_results != NULL)
  {
    W6X_WiFi_PrintScan(scan_results);
  }

  if ((scan_results == NULL) || (scan_results->Count == 0U))
  {
    LogInfo("No scan results\n");
    memset(&g_ble_comm_app_ctx->scan_results, 0, sizeof(g_ble_comm_app_ctx->scan_results));
  }
  else
  {
    memset(&g_ble_comm_app_ctx->scan_results, 0, sizeof(g_ble_comm_app_ctx->scan_results));
    g_ble_comm_app_ctx->scan_results.Count = scan_results->Count;
    g_ble_comm_app_ctx->scan_results.AP = scan_results->AP;
  }

  if (g_ble_comm_app_ctx->scan_event_flags != NULL)
  {
    xEventGroupSetBits(g_ble_comm_app_ctx->scan_event_flags, EVENT_FLAG_WIFI_SCAN_DONE);
  }
}

/**
  * @brief  Build the BLE payload used to report one Wi-Fi access point.
  * @param  dst Destination buffer where the payload will be written.
  * @param  dst_size Size of the destination buffer in bytes.
  * @param  ssid Pointer to the null-terminated SSID string.
  * @param  channel Wi-Fi channel of the access point.
  * @param  rssi RSSI level of the access point.
  * @param  security Security flags associated with the access point.
  * @retval Payload size in bytes on success, or 0 on error.
  * @note   Payload format is:
  *         - byte 0: SSID length including terminating '\0'
  *         - byte 1: channel
  *         - bytes 2..3: RSSI
  *         - bytes 4..7: security flags
  *         - bytes 8..N: SSID including terminating '\0'
  */
static uint8_t BLE_COMM_APP_BuildApScanPayload(uint8_t *dst,
                                               size_t dst_size,
                                               const uint8_t *ssid,
                                               uint8_t channel,
                                               int16_t rssi,
                                               uint32_t security)
{
  size_t ssid_len = 0U;
  size_t payload_len = 0U;
  uint16_t rssi_u16 = 0U;

  if ((dst == NULL) || (ssid == NULL))
  {
    return 0U;
  }

  ssid_len = strnlen((const char *)ssid, W6X_WIFI_MAX_SSID_SIZE);

  memset(dst, 0, dst_size);

  /* BLE payload format:
   * [0]   = SSID_len including terminating '\0'
   * [1]   = Channel
   * [2:3] = RSSI
   * [4:7] = Security
   * [8..] = SSID bytes including terminating '\0'
   */
  payload_len = 8U + ssid_len + 1U;
  if ((payload_len > dst_size) || (payload_len > UINT8_MAX))
  {
    return 0U;
  }

  dst[0] = (uint8_t)(ssid_len + 1U);
  dst[1] = channel;

  rssi_u16 = (uint16_t)rssi;
  dst[2] = (uint8_t)(rssi_u16 & 0xFFU);
  dst[3] = (uint8_t)((rssi_u16 >> 8) & 0xFFU);

  dst[4] = (uint8_t)(security & 0xFFU);
  dst[5] = (uint8_t)((security >> 8) & 0xFFU);
  dst[6] = (uint8_t)((security >> 16) & 0xFFU);
  dst[7] = (uint8_t)((security >> 24) & 0xFFU);

  memcpy(&dst[8], ssid, ssid_len);
  dst[8U + ssid_len] = '\0';

  return (uint8_t)payload_len;
}

/**
  * @brief  Send the current Wi-Fi scan report payload through BLE notification.
  * @retval None
  * @note   The payload is sent on the Wi-Fi AP list characteristic using the
  *         size and data previously prepared in the shared application context.
  */
static void BLE_COMM_APP_SendWifiScanReportNotification(void)
{
  int32_t ret = W6X_STATUS_OK;
  uint32_t SentDatalen = 0U;

  LogInfo("BLE Send Notification size=%u\n", g_ble_comm_app_ctx->wifi_comm_update_char_size);
  ret = W6X_Ble_ServerSendNotification(WIFI_COMMISSIONING_SERVICE_INDEX,
                                       WIFI_AP_LIST_CHAR_INDEX,
                                       g_ble_comm_app_ctx->wifi_comm_update_char_data,
                                       g_ble_comm_app_ctx->wifi_comm_update_char_size,
                                       &SentDatalen,
                                       6000);
  if (ret != W6X_STATUS_OK)
  {
    LogError("Send Notification FAILED: %" PRIi32 "\n", ret);
  }
}

/**
  * @brief  Send the current Wi-Fi monitoring payload through BLE notification.
  * @retval None
  * @note   The payload is sent on the Wi-Fi monitoring characteristic using the
  *         size and data previously prepared in the shared application context.
  */
static void BLE_COMM_APP_SendWifiMonitoringNotification(void)
{
  int32_t ret = W6X_STATUS_OK;
  uint32_t SentDatalen = 0U;

  LogInfo("BLE Send Notification size=%u\n", g_ble_comm_app_ctx->wifi_comm_update_char_size);
  ret = W6X_Ble_ServerSendNotification(WIFI_COMMISSIONING_SERVICE_INDEX,
                                       WIFI_MONITORING_CHAR_INDEX,
                                       g_ble_comm_app_ctx->wifi_comm_update_char_data,
                                       g_ble_comm_app_ctx->wifi_comm_update_char_size,
                                       &SentDatalen,
                                       6000);
  if (ret != W6X_STATUS_OK)
  {
    LogError("Send Notification FAILED: %" PRIi32 "\n", ret);
  }
}
