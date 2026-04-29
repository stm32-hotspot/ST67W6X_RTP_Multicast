/**
 *******************************************************************************
 * @file    main_app.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Main application
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#include <inttypes.h>
#include <string.h>

#include "app_config.h"
#include "camera_app.h"
#include "common_parser.h"
#include "logging.h"
#include "logshell_ctrl.h"
#include "main.h"
#include "main_app.h"
#include "shell.h"
#include "spi_iface.h"
#include "w6x_rtp_sender.h"
#include "w6x_version.h"

/*ADDED BY OC BEGIN*/
#if ST67_ARCH == W6X_ARCH_T02
  #include "lwip.h"
  #include "lwip/sockets.h"
  #include <lwip/api.h>
#endif /* ST67_ARCH */
/*ADDED BY OC END*/

/* Private tunables ----------------------------------------------------------*/
//#define REMOTE_IP               0xC0A808C6UL  /* 192.168.8.198 Unicast*/
#define REMOTE_IP                 0xEF000000UL  /* 239.0.0.0 Multicast */
//#define REMOTE_IP               0xFFFFFFFFUL  /* 255.255.255.255 Broadcast */

#define REMOTE_RTP_PORT           RTP_PORT
#define REMOTE_RTCP_PORT          (RTP_PORT + 1U)

#define APP_SELECTION             APP_H264_RTP_STREAM

/* Private definitions -------------------------------------------------------*/

#define WIFI_SCAN_TIMEOUT         10000                                           /*!< Delay before to declare the scan in failure */
#define WIFI_LOSS_PAYLOAD_SIZE    (UDP_PAYLOAD_MAX_SIZE - sizeof(t_loss_header))  /*!< Maximum payload size in bytes for loss test */
#define WIFI_DTIM                 1                                               /*!< DTIM period */

/* App events */
#define APP_EVT_WIFI_SCAN_READY   (1U << 0U)

/* App selection */
#define APP_LWIP_TEST             0U
#define APP_RTP_TEST              1U
#define APP_LOSS_DUMMY_TEST       2U
#define APP_LOSS_H264_TEST        3U
#define APP_H264_UDP_STREAM       4U
#define APP_H264_RTP_STREAM       5U

/* Private macros ------------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

#if (APP_SELECTION == APP_LOSS_DUMMY_TEST) || (APP_SELECTION == APP_LOSS_H264_TEST)
/** Header to keep track of lost frames */
typedef struct
{
  uint16_t timestamp;
  uint16_t size;
  uint16_t frame;
  uint8_t  index;
  uint8_t  count;
} t_loss_header;
#endif /* APP_SELECTION */

/**
  * @brief  Structure to store an Access Point information
  */
typedef struct
{
  uint8_t SSID_len;                         /*!< Service Set Identifier length */
  uint8_t Channel;                          /*!< Wi-Fi channel */
  int16_t RSSI;                             /*!< Signal strength of Wi-Fi spot */
  uint32_t Security;                        /*!< Security of Wi-Fi spot */
  uint8_t SSID[W6X_WIFI_MAX_SSID_SIZE + 1]; /*!< Service Set Identifier value. Wi-Fi spot name */
} APP_AP_t;

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
} APP_Ble_Data_t;

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
} APP_Ble_Char_t;

/**
  * @brief  BLE write event structure
  */
typedef struct
{
  uint8_t conn_handle;                      /*!< Connection handle */
  uint8_t charac_idx;                       /*!< Characteristic index */
  uint32_t data_len;                        /*!< Length of the data */
  uint8_t *data;                            /*!< Data received */
} APP_Ble_WriteEvent_t;

/**
  * @brief  Application information structure
  */
typedef struct
{
  char *version;                  /*!< Version of the application */
  char *name;                     /*!< Name of the application */
} APP_Info_t;

/* Application events */
/** Event when user button is pressed */
#define EVT_APP_BUTTON                              (1<<0)
/** Event when BLE is connected */
#define EVT_APP_BLE_CONNECTED                       (1<<1)
/** Event when BLE is disconnected */
#define EVT_APP_BLE_DISCONNECTED                    (1<<2)
/** Event when BLE connection parameters are updated */
#define EVT_APP_BLE_CONNECTION_PARAM_UPDATE         (1<<3)
/** Event when BLE characteristic is read */
#define EVT_APP_BLE_READ                            (1<<4)
/** Event when BLE characteristic is written */
#define EVT_APP_BLE_WRITE                           (1<<5)
/** Event when BLE service is found */
#define EVT_APP_BLE_SERVICE_FOUND                   (1<<6)
/** Event when BLE characteristic is found */
#define EVT_APP_BLE_CHAR_FOUND                      (1<<7)
/** Event when BLE indication is enabled */
#define EVT_APP_BLE_INDICATION_STATUS_ENABLED       (1<<8)
/** Event when BLE indication is disabled */
#define EVT_APP_BLE_INDICATION_STATUS_DISABLED      (1<<9)
/** Event when BLE notification is enabled */
#define EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED     (1<<10)
/** Event when BLE notification is disabled */
#define EVT_APP_BLE_NOTIFICATION_STATUS_DISABLED    (1<<11)
/** Event when BLE notification data is received */
#define EVT_APP_BLE_NOTIFICATION_DATA               (1<<12)
/** Event when BLE MTU size is updated */
#define EVT_APP_BLE_MTU_SIZE                        (1<<13)
/** Event when BLE pairing failed */
#define EVT_APP_BLE_PAIRING_FAILED                  (1<<14)
/** Event when BLE pairing is completed */
#define EVT_APP_BLE_PAIRING_COMPLETED               (1<<15)
/** Event when BLE pairing confirmation is requested */
#define EVT_APP_BLE_PAIRING_CONFIRM                 (1<<16)
/** Event when BLE passkey entry is requested */
#define EVT_APP_BLE_PASSKEY_ENTRY                   (1<<17)
/** Event when BLE passkey is displayed */
#define EVT_APP_BLE_PASSKEY_DISPLAYED               (1<<18)
/** Event when BLE passkey confirmation is requested */
#define EVT_APP_BLE_PASSKEY_CONFIRM                 (1<<19)
/** Event when BLE pairing is canceled */
#define EVT_APP_BLE_PAIRING_CANCELED                (1<<20)
/** Event when Wi-Fi is connected to an Access Point */
#define EVT_APP_WIFI_CONNECTED                      (1<<21)
/** Event when Wi-Fi connection timeout */
#define EVT_APP_WIFI_CONNECTION_TIMEOUT             (1<<22)

/** Wi-Fi scan done event flag */
#define EVENT_FLAG_WIFI_SCAN_DONE                   (1<<1)

/** Application events bitmask */
#define EVT_APP_ALL_BIT         (EVT_APP_BUTTON | \
                                 EVT_APP_BLE_CONNECTED  | \
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
/** BLE Wi-Fi Commissioning Service UUID */
#define WIFI_COMMISSIONING_SERVICE_UUID          "0000ff9acc7a482a984a7f2ed5b3e58f"
/** BLE Wi-Fi Control Characteristic UUID */
#define WIFI_CONTROL_CHAR_UUID                   "0000fe9b8e2245419d4c21edae82ed19"
/** BLE Wi-Fi Configure Characteristic UUID */
#define WIFI_CONFIGURE_CHAR_UUID                 "0000fe9c8e2245419d4c21edae82ed19"
/** BLE Wi-Fi AP List Characteristic UUID */
#define WIFI_AP_LIST_CHAR_UUID                   "0000fe9d8e2245419d4c21edae82ed19"
/** BLE Wi-Fi Monitoring Characteristic UUID */
#define WIFI_MONITORING_CHAR_UUID                "0000fe9e8e2245419d4c21edae82ed19"


/* Wi-Fi Control Characteristic first byte possible values */
#define CONTROL_ACTION_START_SCAN                0x1  /*!< Start Wi-Fi scan */
#define CONTROL_ACTION_CONNECT                   0x3  /*!< Connect to Wi-Fi */
#define CONTROL_ACTION_DISCONNECT                0x4  /*!< Disconnect from Wi-Fi */
#define CONTROL_ACTION_PING                      0x5  /*!< Ping Wi-Fi */
#if (TEST_AUTOMATION_ENABLE == 1)
#define CONTROL_ACTION_QUIT_CONTROL              0xFF /*!< Quit automation */
#endif /* TEST_AUTOMATION_ENABLE */

/* Wi-Fi Configure Characteristic first byte possible values */
#define CONFIGURE_TYPE_SSID                      0x1  /*!< Configure SSID */
#define CONFIGURE_TYPE_PWD                       0x2  /*!< Configure Password */
#define CONFIGURE_TYPE_SECURITY_FLAG             0x5  /*!< Configure Security Flag */

/* Wi-Fi Monitoring Characteristic first byte possible values */
#define MONITORING_TYPE_CONNECTING               0x3  /*!< Connecting to Wi-Fi */
#define MONITORING_TYPE_CONNECTION_DONE          0x4  /*!< Connection done */
#define MONITORING_TYPE_PING_RESPONSE            0x5  /*!< Ping response */
#define MONITORING_TYPE_ERROR                    0x6  /*!< Error */

/* Wi-Fi Monitoring Characteristic second byte possible values */
#define MONITORING_DATA_CONNECTION_TIMEOUT       0x1  /*!< Connection timeout */

/** Delay before to declare the scan in failure */
#define WIFI_SCAN_TIMEOUT                        10000

/** Number of ping requests to send */
#define WIFI_PING_COUNT                          4
/** Size of the ping request */
#define WIFI_PING_SIZE                           64
/** Time interval between two ping requests */
#define WIFI_PING_INTERVAL                       1000
/** Percent convert number */
#define WIFI_PING_PERCENT                        100

/** Generic Passkey */
#define BLE_GENERIC_PASSKEY                      123456


#ifndef ADDR2STR
/** BD Address buffer to string macros */
#define ADDR2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
/** BD Address string format */
#define ADDRSTR "%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16 ":%02" PRIx16
#endif /* ADDR2STR */

/* Private data --------------------------------------------------------------*/

/** BLE data buffer to receive message from the ST67W6X Driver */
static uint8_t a_APP_AvailableData[247] = {0};

/** Wi-Fi Commissioning update characteristic data */
uint8_t a_APP_WifiCommUpdateCharData[247] = {0};

/** Wi-Fi connected SSID */
uint8_t a_APP_WifiConnectedSSID[W6X_WIFI_MAX_SSID_SIZE + 1] = {0};

/** Wi-Fi scan options */
W6X_WiFi_Scan_Opts_t APP_Opts = {0};

/** Wi-Fi connect options */
W6X_WiFi_Connect_Opts_t APP_ConnectOpts = {0};

/** Wi-Fi connection state */
W6X_WiFi_StaStateType_e sta_state   = W6X_WIFI_STATE_STA_OFF;

/** Size of Wi-Fi Commissioning update characteristic data */
uint8_t size_of_WifiCommUpdateCharData;

/** Wi-Fi scan results */
W6X_WiFi_Scan_Result_t app_scan_results = {0};

/** Wi-Fi scan event flags */
static EventGroupHandle_t scan_event_flags = NULL;

/** Queue to handle BLE write events for FUOTA */
QueueHandle_t bleWriteQueue = NULL;

/** Advertising Data */
char a_AdvData[36] =
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

/** BLE parameters */
APP_Ble_Data_t app_ble_params =
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
  .available_data_length = 0,      /* Initialize available data length to 0 */
};

/** BLE characteristics */
static const APP_Ble_Char_t app_ble_char[] =
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
W6X_Ble_Bonded_Devices_Result_t app_ble_BondedDeviceList = {0};

/** Application information */
static const APP_Info_t app_info =
{
  .name = "ST67W6X RTP Video Streaming with Wi-Fi Commissioning over BLE",
  .version = W6X_VERSION_STR
};


static EventGroupHandle_t app_evt_current = NULL;

#if (APP_SELECTION == APP_LOSS_DUMMY_TEST) || (APP_SELECTION == APP_LOSS_H264_TEST)
static uint8_t            _loss_payload[UDP_PAYLOAD_MAX_SIZE] = { 0 };
static volatile uint32_t  _loss_delay = 1U;
#endif /* APP_SELECTION */

/* Private function ----------------------------------------------------------*/

static void APP_error_cb(W6X_Status_t ret_w6x, char const *func_name);
static void APP_setevent(EventGroupHandle_t *app_event, uint32_t evt);
static void APP_ble_cb(W6X_event_id_t event_id, void *event_args);
static void APP_mqtt_cb(W6X_event_id_t event_id, void *event_args);
static void APP_net_cb(W6X_event_id_t event_id, void *event_args);
static void APP_wifi_cb(W6X_event_id_t event_id, void *event_args);
static void APP_wifi_scan_cb(int32_t status, W6X_WiFi_Scan_Result_t *scan);

static void Run_Application(void);

#if APP_SELECTION == APP_LWIP_TEST
extern int32_t lwip_socket_test(void);

#elif APP_SELECTION == APP_RTP_TEST
extern int32_t w6x_rtp_test(void);

#elif APP_SELECTION == APP_LOSS_DUMMY_TEST
static int32_t w6x_loss_dummy_test(void);

#elif APP_SELECTION == APP_LOSS_H264_TEST
static int32_t w6x_loss_h264_test(void);

#elif APP_SELECTION == APP_H264_UDP_STREAM
static int32_t w6x_h264_udp_stream(void);

#elif APP_SELECTION == APP_H264_RTP_STREAM
static int32_t w6x_h264_rtp_stream(void);

#endif /* APP_SELECTION */

extern void     EWLPoolChoiceCb(uint8_t **pool, size_t *size);
extern void     EWLPoolReleaseCb(uint8_t **pool);
extern void     HAL_GPIO_EXTI_Callback(uint16_t pin);
extern void     HAL_GPIO_EXTI_Rising_Callback(uint16_t pin);
extern void     HAL_GPIO_EXTI_Falling_Callback(uint16_t pin);
extern int32_t  w6x_socket_udp_create(uint8_t bind, uint16_t port);

/* Public API definitions ----------------------------------------------------*/

void main_app(void* args)
{
  W6X_WiFi_Connect_t      conn_data   = {0};
  int32_t                 status      = 0;

  EventBits_t eventBits = 0;

  int32_t data_index = 0;
  uint8_t bd_address[W6X_BLE_BD_ADDR_SIZE];
  const char hex_chars[] = "0123456789ABCDEF";
  char ble_device_name[W6X_BLE_DEVICE_NAME_SIZE];

  app_evt_current = xEventGroupCreate();

  /* Initialize the logging utilities */
  LoggingInit();

  LogInfo("#### Welcome to %s Application #####\n", app_info.name);
  LogInfo("# build: %s %s\n", __TIME__, __DATE__);
  LogInfo("--------------- Host info ---------------\n");
  LogInfo("Host FW Version:          %s\n", app_info.version);

  /* Register the application callback to received events from ST67W6X Driver */
  W6X_App_Cb_t App_cb = {0};
  App_cb.APP_wifi_cb  = APP_wifi_cb;
  App_cb.APP_net_cb   = APP_net_cb;
  App_cb.APP_ble_cb   = APP_ble_cb;
  App_cb.APP_mqtt_cb  = APP_mqtt_cb;
  App_cb.APP_error_cb = APP_error_cb;
  W6X_RegisterAppCb(&App_cb);

  /* Initialize ST67W6X */
  status = W6X_Init();
  if (status)
  {
    LogError("Failed to initialize ST67W6X Driver, %" PRIi32 "\n", status);
    goto _err;
  }
  status = W6X_WiFi_Init();
  if (status)
  {
    LogError("Failed to initialize ST67W6X Wifi component, %" PRIi32 "\n", status);
    goto _err;
  }
  LogInfo("Wifi init ready!\n");
  status = MX_LWIP_Init();
  if (status)
  {
    LogError("Failed to initialize LWIP stack, %" PRIi32 "\n", status);
    goto _err;
  }

  /* Initialize the ST67W6X BLE module */
  status = W6X_Ble_Init(W6X_BLE_MODE_SERVER, a_APP_AvailableData, sizeof(a_APP_AvailableData) - 1);
  if (status)
  {
    LogError("Failed to initialize ST67W6X BLE component, %" PRIi32 "\n", status);
    goto _err;
  }

  LogInfo("Ble init is done\n");

  status = W6X_Ble_GetBDAddress(bd_address);
  if (status == W6X_STATUS_OK)
  {
    LogInfo("BD Address: " MACSTR "\n", MAC2STR(bd_address));
  }
  else
  {
    LogError("BD Address identification failed, %" PRIi32 "\n", status);
    goto _err;
  }

  sprintf(ble_device_name, W6X_BLE_HOSTNAME "_%02" PRIX16, bd_address[5]);
  status = W6X_Ble_SetDeviceName(ble_device_name);
  if (status)
  {
    LogError("Failed to set device name, %" PRIi32 "\n", status);
    goto _err;
  }

  /* Configure the ST67W6X BLE module
   * - Set the Tx power to 0 (minimal)
   * - Set the Bluetooth Device Address
   * - Set the Advertising data
   */
  LogInfo("Configure BLE\n");
  status = W6X_Ble_SetTxPower(0);
  if (status)
  {
    LogError("Failed to set TX power, %" PRIi32 "\n", status);
    goto _err;
  }

  /* Fill adv data with BD Address */
  while (data_index < 6)
  {
    a_AdvData[20 + (2 * data_index)] = hex_chars[(bd_address[data_index] & 0xF0) >> 4];
    a_AdvData[20 + (2 * data_index) + 1] = hex_chars[bd_address[data_index] & 0x0F];
    data_index++;
  }
  status = W6X_Ble_SetAdvData((const char *) a_AdvData);
  if (status)
  {
    LogError("Failed to set ADV data, %" PRIi32 "\n", status);
    goto _err;
  }
  LogInfo("BLE configuration is done\n");

  /* Create the BLE Commissioning Service */
  LogInfo("\nBLE Commissioning Service Creation\n");
  status = W6X_Ble_CreateService(WIFI_COMMISSIONING_SERVICE_INDEX,
                              WIFI_COMMISSIONING_SERVICE_UUID,
                              W6X_BLE_UUID_TYPE_128);

  if (status)
  {
    LogError("Failed to Create service, %" PRIi32 "\n", status);
    goto _err;
  }


  /* Create the BLE Characteristics */
  for (uint8_t i = 0; i < sizeof(app_ble_char) / sizeof(APP_Ble_Char_t); i++)
  {
	status = W6X_Ble_CreateCharacteristic(app_ble_char[i].service_index, app_ble_char[i].char_index,
                                       app_ble_char[i].char_uuid, app_ble_char[i].uuid_type,
                                       app_ble_char[i].char_property, app_ble_char[i].char_permission);

    if (status != 0)
    {
      LogError("Failed to create %s, %" PRIi32 "\n", app_ble_char[i].desc, status);
      goto _err;
    }
    LogInfo("- %s created\n", app_ble_char[i].desc);
  }

  /* Register the BLE Characteristics */
  status = W6X_Ble_RegisterCharacteristics();

  if (status != 0)
  {
    LogError("Failed to Register characteristics, %" PRIi32 "\n", status);
    goto _err;
  }
  LogInfo("- BLE services and charac registered\n");
  LogInfo("BLE service and charac creation is done\n");

  /* Setup security mode */
  status = W6X_Ble_SetSecurityParam(W6X_BLE_SEC_IO_KEYBOARD_DISPLAY);
  if (status)
  {
    LogError("BLE Set Security Parameters failed, %" PRIi32 "\n", status);
  }

  /* Start the BLE Advertising mode: the ST67W6X can be detected as BLE device */
  LogInfo("\nStart BLE advertising\n");
  status = W6X_Ble_AdvStart();
  if (status)
  {
    LogError("Failed to start advertising, %" PRIi32 "\n", status);
    goto _err;
  }

  LogInfo("BLE advertising is started\n");


  while (1)
  {
    /* Wait to receive a BLE event */
    eventBits = xEventGroupWaitBits(app_evt_current, EVT_APP_ALL_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    /* Process button */
    if (eventBits & EVT_APP_BUTTON)
    {
      if (app_ble_params.remote_ble_device.IsConnected != 0x00)
      {
    	status = W6X_Ble_Disconnect(app_ble_params.remote_ble_device.conn_handle);
        if (status)
        {
          LogError("BLE disconnection failed, %" PRIi32 "\n", status);
        }
      }
      else
      {
        LogInfo("Unpair bonded devices\n");
        W6X_Ble_SecurityGetBondedDeviceList(&app_ble_BondedDeviceList);
        while (app_ble_BondedDeviceList.Count != 0)
        {
          uint32_t count = app_ble_BondedDeviceList.Count - 1;
          LogInfo("Bonded Device: " ADDRSTR "\n",
                  ADDR2STR(app_ble_BondedDeviceList.Bonded_device[count].BDAddr));

          status = W6X_Ble_SecurityUnpair(app_ble_BondedDeviceList.Bonded_device[count].BDAddr,
                                       W6X_BLE_PUBLIC_ADDR);
          if (status)
          {
            LogError("failed to unpair bonded devices, %" PRIi32 "\n", status);
          }
          LogInfo(ADDRSTR " unpaired\n",
                  ADDR2STR(app_ble_BondedDeviceList.Bonded_device[count].BDAddr));
          W6X_Ble_SecurityGetBondedDeviceList(&app_ble_BondedDeviceList);
        }

        LogInfo("No more bonded device\n");
      }
    }

    /* Process BLE notification */
    if (eventBits & EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED)
    {
      if (a_APP_WifiConnectedSSID[0] != 0)
      {
        /* Retrieve information about Wifi network connected */
        /* Send notification when Wi-Fi connected */
        uint8_t monitoring_data[W6X_WIFI_MAX_SSID_SIZE + 1] = {0};
        /* Payload length = Flag Length + SSID length */
        size_of_WifiCommUpdateCharData = strlen((char *)a_APP_WifiConnectedSSID) + 1;
        /* Add Connection Done Flag at the beginning of the payload */
        monitoring_data[0] = MONITORING_TYPE_CONNECTION_DONE;
        /* Copy SSID of connected device */
        strncpy((char *) &monitoring_data[1], (char *)a_APP_WifiConnectedSSID, size_of_WifiCommUpdateCharData - 1);
        memcpy(a_APP_WifiCommUpdateCharData, (void *) &monitoring_data, size_of_WifiCommUpdateCharData);

        BLE_Send_Wifi_Monitoring_Notification();
      }
    }

//    /* Process BLE disconnection */
//    if (eventBits & EVT_APP_BLE_DISCONNECTED)
//    {
//        Run_Application();
//    }

    /* Process BLE read */
    if (eventBits & EVT_APP_BLE_READ)
    {
    }

    /* Process BLE write */
    if (eventBits & EVT_APP_BLE_WRITE)
    {
      if ((app_ble_params.service_idx == WIFI_COMMISSIONING_SERVICE_INDEX) &
          (app_ble_params.charac_idx == WIFI_CONTROL_CHAR_INDEX))
      {
        LogInfo("WIFI Control Charac\n");
        if (a_APP_AvailableData[0] == CONTROL_ACTION_START_SCAN) /* Start Scan */
        {
          LogInfo("WIFI Scan Enable\n");
          /* Run a Wi-Fi scan to retrieve the list of all nearby Access Points */
          scan_event_flags = xEventGroupCreate();
          W6X_WiFi_Scan(&APP_Opts, &APP_wifi_scan_cb);

          /* Wait to receive the EVENT_FLAG_SCAN_DONE event. The scan is declared as failed after 'ScanTimeout' delay */
          if ((int32_t)xEventGroupWaitBits(scan_event_flags, EVENT_FLAG_WIFI_SCAN_DONE, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT)) != EVENT_FLAG_WIFI_SCAN_DONE)
          {
            LogError("Scan Failed\n");
            goto _err;
          }
          else
          {
            /* Retrieve the configuration of nearby Access Point */
            for (uint32_t ap_index = 0; ap_index < app_scan_results.Count; ap_index++)
            {
              if (strlen((char *)app_scan_results.AP[ap_index].SSID) > 0)
              {
                APP_AP_t *scan_item = (APP_AP_t *)a_APP_WifiCommUpdateCharData;
                scan_item->Channel = app_scan_results.AP[ap_index].Channel;
                scan_item->Security = app_scan_results.AP[ap_index].Security;
                scan_item->RSSI = app_scan_results.AP[ap_index].RSSI;
                memcpy(scan_item->SSID, app_scan_results.AP[ap_index].SSID, sizeof(scan_item->SSID));
                scan_item->SSID_len = W6X_WIFI_MAX_SSID_SIZE;
                size_of_WifiCommUpdateCharData = sizeof(APP_AP_t);

                /* Send the configuration of each Access Point as BLE notification */
                BLE_Send_Wifi_Scan_Report_Notification();
              }
            }
          }
        }
        else if (a_APP_AvailableData[0] == CONTROL_ACTION_CONNECT) /* Connect */
        {
          /* Connect the device to the selected Access Point */
          LogInfo("WIFI Connect\n");
          status = W6X_WiFi_Connect(&APP_ConnectOpts);
          if (status)
          {
            LogError("failed to connect, %" PRIi32 "\n", status);
            APP_setevent(&app_evt_current, EVT_APP_WIFI_CONNECTION_TIMEOUT);
          }
          else
          {
            LogInfo("App connected\n");
            APP_setevent(&app_evt_current, EVT_APP_WIFI_CONNECTED);
          }
        }
        else if (a_APP_AvailableData[0] == CONTROL_ACTION_DISCONNECT) /* Disconnect */
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
        else if (a_APP_AvailableData[0] == CONTROL_ACTION_PING) /* Ping */
        {

        }
      }
    }

    if (eventBits & EVT_APP_WIFI_CONNECTED)
    {
      if (W6X_WiFi_Station_GetState(&sta_state, &conn_data) != W6X_STATUS_OK)
      {
        LogInfo("Connected to an Access Point\n");
        return;
      }

      memcpy(a_APP_WifiConnectedSSID, conn_data.SSID, sizeof(a_APP_WifiConnectedSSID));

      LogInfo("Connected to following Access Point :\n");
      LogInfo("[" MACSTR "] Channel: %" PRIu32 " | RSSI: %" PRIi32 " | SSID: %s\n",
              MAC2STR(conn_data.MAC),
			  conn_data.Channel,
			  conn_data.Rssi,
			  conn_data.SSID);

      /* Send notification when Wi-Fi connected */
      uint8_t monitoring_data[W6X_WIFI_MAX_SSID_SIZE + 1] = {0};

      /* Payload length = Flag Length + SSID length */
      size_of_WifiCommUpdateCharData = strlen((char *)APP_ConnectOpts.SSID) + 1;

      /* Add Connection Done Flag at the beginning of the payload */
      monitoring_data[0] = MONITORING_TYPE_CONNECTION_DONE;

      /* Copy SSID of connected device */
      strncpy((char *) &monitoring_data[1], (char *)APP_ConnectOpts.SSID, size_of_WifiCommUpdateCharData - 1);
      memcpy(a_APP_WifiCommUpdateCharData, (void *) &monitoring_data, size_of_WifiCommUpdateCharData);

      BLE_Send_Wifi_Monitoring_Notification();
      Run_Application();

    }

    if (eventBits & EVT_APP_WIFI_CONNECTION_TIMEOUT)
    {
      /* Send notification if Wi-Fi connection error */
      uint8_t monitoring_data[2] = {MONITORING_TYPE_ERROR, MONITORING_DATA_CONNECTION_TIMEOUT};
      size_of_WifiCommUpdateCharData = 2;
      memcpy(a_APP_WifiCommUpdateCharData, (void *) &monitoring_data, size_of_WifiCommUpdateCharData);

      BLE_Send_Wifi_Monitoring_Notification();
    }

    /* Process BLE passkey entry */
    if (eventBits & EVT_APP_BLE_PASSKEY_ENTRY)
    {
      app_ble_params.PassKey = BLE_GENERIC_PASSKEY;
      if (app_ble_params.remote_ble_device.conn_handle != 0xff)
      {
    	status = W6X_Ble_SecuritySetPassKey(app_ble_params.remote_ble_device.conn_handle, app_ble_params.PassKey);
        if (status)
        {
          LogError("Failed to set passkey, %" PRIi32 "\n"
                   "Press button while disconnected to clear security Database and relaunch pairing process\n", status);
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

    /* Process BLE passkey confirm */
    if (eventBits & EVT_APP_BLE_PASSKEY_CONFIRM)
    {
      if (app_ble_params.remote_ble_device.conn_handle != 0xff)
      {
    	status = W6X_Ble_SecurityPassKeyConfirm(app_ble_params.remote_ble_device.conn_handle);
        if (status)
        {
          LogError("Failed to send passkey confirm, %" PRIi32 "\n"
                   "Press button while disconnected to clear security Database and relaunch pairing process\n", status);
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

    /* Process BLE pairing confirm */
    if (eventBits & EVT_APP_BLE_PAIRING_CONFIRM)
    {
      if (app_ble_params.remote_ble_device.conn_handle != 0xff)
      {
    	status = W6X_Ble_SecurityPairingConfirm(app_ble_params.remote_ble_device.conn_handle);
        if (status)
        {
          LogError("Pairing Confirm Failed, %" PRIi32 "\n"
                   "Press button while disconnected to clear security Database and relaunch pairing process\n", status);
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

    /* Process BLE pairing complete */
    if (eventBits & EVT_APP_BLE_PAIRING_COMPLETED)
    {
      LogInfo("Pairing Completed\n");
    }
  }




_err:
  W6X_WiFi_DeInit();

  /* Avoid to fully close: Errors on auxiliary tasks */
  LogInfo("\nGoing IDLE!\n");
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }

  /* De-initialize the ST67W6X Driver */
  W6X_DeInit();
  LogInfo("\nApplication end\n");
}

/* Private function definitions ----------------------------------------------*/

/**
 * @brief W6X error callback
 * @param ret_w6x: W6X status
 * @param func_name: function name
 */
static void APP_error_cb(W6X_Status_t ret_w6x, char const *func_name)
{
  LogError("[%s] in %s API\n", W6X_StatusToStr(ret_w6x), func_name);
}

/**
 * @brief BLE event callback
 * @param event_id: Event ID
 * @param event_args: Event arguments
 */
static void APP_ble_cb(W6X_event_id_t event_id, void *event_args)
{
  /* USER CODE BEGIN APP_ble_cb_1 */

  /* USER CODE END APP_ble_cb_1 */
  W6X_Ble_CbParamData_t *p_param_ble_data = (W6X_Ble_CbParamData_t *) event_args;

  switch (event_id)
  {
    case W6X_BLE_EVT_CONNECTED_ID:
      LogInfo(" -> BLE CONNECTED [Conn_Handle: %" PRIu16 "]\n", p_param_ble_data->remote_ble_device.conn_handle);

      /* Fill remote device structure */
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.remote_ble_device.IsConnected = p_param_ble_data->remote_ble_device.IsConnected;
      memcpy(app_ble_params.remote_ble_device.BDAddr, p_param_ble_data->remote_ble_device.BDAddr,
             sizeof(app_ble_params.remote_ble_device.BDAddr));
      memcpy(app_ble_params.remote_ble_device.DeviceName, p_param_ble_data->remote_ble_device.DeviceName,
             sizeof(app_ble_params.remote_ble_device.DeviceName));
      memcpy(app_ble_params.remote_ble_device.ManufacturerData, p_param_ble_data->remote_ble_device.ManufacturerData,
             sizeof(app_ble_params.remote_ble_device.ManufacturerData));

      APP_setevent(&app_evt_current, EVT_APP_BLE_CONNECTED);
      break;

    case W6X_BLE_EVT_CONNECTION_PARAM_ID:
      LogInfo(" -> BLE CONNECTION PARAM UPDATE [Conn_Handle: %" PRIu16 "]\n",
              p_param_ble_data->remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_CONNECTION_PARAM_UPDATE);
      break;

    case W6X_BLE_EVT_DISCONNECTED_ID:
      LogInfo(" -> BLE DISCONNECTED [Conn_Handle: %" PRIu16 "]\n", p_param_ble_data->remote_ble_device.conn_handle);

      /* Reinitialize remote device struct */
      app_ble_params.remote_ble_device.RSSI = 0;
      app_ble_params.remote_ble_device.IsConnected = 0;
      app_ble_params.remote_ble_device.conn_handle = 0xff;
      memset(app_ble_params.remote_ble_device.DeviceName, 0, sizeof(app_ble_params.remote_ble_device.DeviceName));
      memset(app_ble_params.remote_ble_device.BDAddr, 0, sizeof(app_ble_params.remote_ble_device.BDAddr));
      memset(app_ble_params.remote_ble_device.ManufacturerData, 0,
             sizeof(app_ble_params.remote_ble_device.ManufacturerData));

      APP_setevent(&app_evt_current, EVT_APP_BLE_DISCONNECTED);
      break;

    case W6X_BLE_EVT_INDICATION_STATUS_ENABLED_ID:
      LogInfo(" -> BLE INDICATION ENABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      APP_setevent(&app_evt_current, EVT_APP_BLE_INDICATION_STATUS_ENABLED);
      break;

    case W6X_BLE_EVT_INDICATION_STATUS_DISABLED_ID:
      LogInfo(" -> BLE INDICATION DISABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      APP_setevent(&app_evt_current, EVT_APP_BLE_INDICATION_STATUS_DISABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_STATUS_ENABLED_ID:
      LogInfo(" -> BLE NOTIFICATION ENABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      APP_setevent(&app_evt_current, EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_STATUS_DISABLED_ID:
      LogInfo(" -> BLE NOTIFICATION DISABLED [Service: %" PRIu16 ", Charac: %" PRIu16 "].\n",
              p_param_ble_data->service_idx, p_param_ble_data->charac_idx);
      APP_setevent(&app_evt_current, EVT_APP_BLE_NOTIFICATION_STATUS_DISABLED);
      break;

    case W6X_BLE_EVT_NOTIFICATION_DATA_ID:
      LogInfo(" -> BLE NOTIFICATION DATA.\n");
      APP_setevent(&app_evt_current, EVT_APP_BLE_NOTIFICATION_DATA);
      break;

    case W6X_BLE_EVT_WRITE_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.service_idx = p_param_ble_data->service_idx;
      app_ble_params.charac_idx = p_param_ble_data->charac_idx;
      app_ble_params.available_data_length = p_param_ble_data->available_data_length;
      LogInfo(" -> BLE WRITE [Conn_Handle: %" PRIu16 ", Service: %" PRIu16 ", Charac: %" PRIu16
              ", length %" PRIu32 "]\n",
              p_param_ble_data->remote_ble_device.conn_handle, p_param_ble_data->service_idx,
              p_param_ble_data->charac_idx, p_param_ble_data->available_data_length);


       for (uint32_t i = 0; i < p_param_ble_data->available_data_length; i++)
       {
         LogInfo("0x%02" PRIX16 "\n", a_APP_AvailableData[i]);
       }


      /* Manage Wi-Fi configuration */
      if ((app_ble_params.service_idx == WIFI_COMMISSIONING_SERVICE_INDEX) &&
          (app_ble_params.charac_idx == WIFI_CONFIGURE_CHAR_INDEX))
      {
        LogInfo("WIFI Configure Charac\n");
        if (a_APP_AvailableData[0] == CONFIGURE_TYPE_SSID) /* Wi-Fi AP SSID */
        {
          memcpy(APP_ConnectOpts.SSID, &a_APP_AvailableData[1], app_ble_params.available_data_length - 1);
          APP_ConnectOpts.SSID[app_ble_params.available_data_length - 1] = '\0';
          LogInfo("SSID NAME\n");
        }

        else if (a_APP_AvailableData[0] == CONFIGURE_TYPE_PWD) /* Wi-Fi AP Password */
        {
          memcpy(APP_ConnectOpts.Password, &a_APP_AvailableData[1], app_ble_params.available_data_length - 1);
          APP_ConnectOpts.Password[app_ble_params.available_data_length - 1] = '\0';
          LogInfo("PWD\n");
        }
      }

      APP_setevent(&app_evt_current, EVT_APP_BLE_WRITE);
      break;

    case W6X_BLE_EVT_READ_ID:
      LogInfo(" -> BLE READ.\n");
      APP_setevent(&app_evt_current, EVT_APP_BLE_READ);
      break;

    case W6X_BLE_EVT_PASSKEY_ENTRY_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      LogInfo(" -> BLE PassKey Entry [Conn_Handle %" PRIu16 "]\n", app_ble_params.remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PASSKEY_ENTRY);
      break;

    case W6X_BLE_EVT_PASSKEY_CONFIRM_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      app_ble_params.PassKey = p_param_ble_data->PassKey;
      LogInfo(" -> BLE PassKey received = %06" PRIu32 " [Conn_Handle %" PRIu16 "]\n", app_ble_params.PassKey,
              app_ble_params.remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PASSKEY_CONFIRM);
      break;

    case W6X_BLE_EVT_PAIRING_CONFIRM_ID:
      app_ble_params.remote_ble_device.conn_handle = p_param_ble_data->remote_ble_device.conn_handle;
      LogInfo(" -> BLE Pairing Confirm [Conn_Handle %" PRIu16 "]\n", app_ble_params.remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PAIRING_CONFIRM);
      break;

    case W6X_BLE_EVT_PAIRING_COMPLETED_ID:
      LogInfo(" -> BLE Pairing Complete\n");
      APP_setevent(&app_evt_current, EVT_APP_BLE_PAIRING_COMPLETED);
      break;

    case W6X_BLE_EVT_PASSKEY_DISPLAY_ID:
      app_ble_params.PassKey = p_param_ble_data->PassKey;
      LogInfo(" -> BLE PASSKEY  = %06" PRIu32 "\n", app_ble_params.PassKey);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PASSKEY_DISPLAYED);
      break;

    case W6X_BLE_EVT_PAIRING_FAILED_ID:
      LogInfo(" -> BLE Pairing Failed [Conn_Handle: %" PRIu16 "]\n", app_ble_params.remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PAIRING_FAILED);
      break;

    case W6X_BLE_EVT_PAIRING_CANCELED_ID:
      LogInfo(" -> BLE Pairing Canceled [Conn_Handle %" PRIu16 "]\n", app_ble_params.remote_ble_device.conn_handle);
      APP_setevent(&app_evt_current, EVT_APP_BLE_PAIRING_CANCELED);
      break;

    default:
      break;
  }
  /* USER CODE BEGIN APP_ble_cb_End */

  /* USER CODE END APP_ble_cb_End */
}

/**
 * @brief MQTT event callback
 * @param event_id: Event ID
 * @param event_args: Event arguments
 */
static void APP_mqtt_cb(W6X_event_id_t event_id, void *event_args)
{
  /* Do nothing */
}

/**
 * @brief Network event callback
 * @param event_id: Event ID
 * @param event_args: Event arguments
 */
static void APP_net_cb(W6X_event_id_t event_id, void *event_args)
{
  /* Do nothing */
}

/**
 * @brief Wi-Fi event callback
 * @param event_id: Event ID
 * @param event_args: Event arguments
 */
static void APP_wifi_cb(W6X_event_id_t event_id, void *event_args)
{
  /* USER CODE BEGIN APP_wifi_cb_1 */

  /* USER CODE END APP_wifi_cb_1 */

  switch (event_id)
  {
    case W6X_WIFI_EVT_CONNECTED_ID:
      APP_setevent(&app_evt_current, EVT_APP_WIFI_CONNECTED);
      break;

    case W6X_WIFI_EVT_DISCONNECTED_ID:
      /* Reinitialize connected device struct */
      memset(APP_ConnectOpts.SSID, 0, W6X_WIFI_MAX_SSID_SIZE + 1);
      memset(APP_ConnectOpts.Password, 0, W6X_WIFI_MAX_PASSWORD_SIZE + 1);
      LogInfo("Station disconnected from Access Point\n");
      memset(a_APP_WifiConnectedSSID, 0, sizeof(a_APP_WifiConnectedSSID));
      break;

    case W6X_WIFI_EVT_REASON_ID:
      /* Reinitialize connected device struct */
      memset(APP_ConnectOpts.SSID, 0, W6X_WIFI_MAX_SSID_SIZE + 1);
      memset(APP_ConnectOpts.Password, 0, W6X_WIFI_MAX_PASSWORD_SIZE + 1);
      LogInfo("Reason: %s\n", W6X_WiFi_ReasonToStr(event_args));
      break;

    default:
      break;
  }
  /* USER CODE BEGIN APP_wifi_cb_End */

  /* USER CODE END APP_wifi_cb_End */
}
/**
 * @brief Wi-Fi scan callback
 * @param status: Scan status
 * @param scan: Scan results
 */
static void APP_wifi_scan_cb(int32_t status, W6X_WiFi_Scan_Result_t *Scan_results)
{
  LogInfo("WiFi Scan done.\n");
  W6X_WiFi_PrintScan(Scan_results);

  if (Scan_results->Count == 0)
  {
    LogInfo("No scan results\n");
  }
  else
  {
    memset(&app_scan_results, 0, sizeof(app_scan_results));
    app_scan_results.Count = Scan_results->Count;
    app_scan_results.AP = Scan_results->AP;
  }
  xEventGroupSetBits(scan_event_flags, EVENT_FLAG_WIFI_SCAN_DONE);
}

static void APP_setevent(EventGroupHandle_t *app_event, uint32_t evt)
{
  /* USER CODE BEGIN APP_setevent_1 */

  /* USER CODE END APP_setevent_1 */
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (xPortIsInsideInterrupt())
  {
    xEventGroupSetBitsFromISR(*app_event, evt, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
    {
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
  }
  else
  {
    xEventGroupSetBits(*app_event, evt);
  }
  /* USER CODE BEGIN APP_setevent_End */

  /* USER CODE END APP_setevent_End */
}

#if APP_SELECTION == APP_LOSS_DUMMY_TEST
/**
 * @brief  Run the UDP loss test with dummy payload
 * @return 0 on success, negative value on error
 */
static int32_t w6x_loss_dummy_test(void)
{
  uint8_t *const  payload = _loss_payload;
  uint8_t         value   = 0x00U;
  int32_t         socket;
  int32_t 		  socket_aux;
  sockaddr_in_t   client = {
    .sin_family     = AF_INET,
    .sin_port       = htons(RTP_PORT),
    .sin_addr.s_addr= htonl(REMOTE_IP),
  };

  sockaddr_in_t   client_aux = {
    .sin_family      = AF_INET,
    .sin_port        = htons(5005), // Send to server port 5005
    .sin_addr.s_addr = htonl(REMOTE_IP),
  };

  /* Prepare */
  socket = w6x_socket_udp_create(0U, 0U);
  if (socket < 0)
  {
    LogError("Failed to create UDP socket, %" PRIi32 "\n", socket);
    return socket;
  }

  /* Prepare aux socket */
  socket_aux = w6x_socket_udp_create(0U, 0U);
  if (socket_aux < 0)
  {
    LogError("Failed to create aux UDP socket, %" PRIi32 "\n", socket_aux);
    //w6x_socket_close(socket_aux);
    return socket_aux;
  }

  /* Stream */
  while (1U)
  {
    memset(payload, value, UDP_PAYLOAD_MAX_SIZE);
    socket_sendto(socket, payload, UDP_PAYLOAD_MAX_SIZE, 0, (const sockaddr_t*)&client, sizeof(client));

    /* Send aux packet (70 bytes) */
    memset(payload, value, 70U);
    socket_sendto(socket_aux, payload, 70U, 0, (const sockaddr_t*)&client_aux, sizeof(client_aux));

    value = (value + 1U) & 0xFFU;
    vTaskDelay(pdMS_TO_TICKS(_loss_delay)); /* Send every second */
  }
}

#elif APP_SELECTION == APP_LOSS_H264_TEST
/**
 * @brief  Run the UDP loss test with h264 payload
 * @return 0 on success, negative value on error
 */
static int32_t w6x_loss_h264_test(void)
{
  t_loss_header *const  header  = (t_loss_header*)_loss_payload;
  uint8_t *const        payload = _loss_payload + sizeof(t_loss_header);
  uint8_t               *start;
  uint8_t               *end;
  size_t                size;
  int32_t               socket;
  sockaddr_in_t         client = {
    .sin_family     = AF_INET,
    .sin_port       = htons(RTP_PORT),
    .sin_addr.s_addr= htonl(REMOTE_IP),
  };

  /* Prepare */
  socket = w6x_socket_udp_create(0U, 0U);
  if (socket < 0)
  {
    LogError("Failed to create UDP socket, %" PRIi32 "\n", socket);
    return socket;
  }

  /* Stream */
  camera_encode_request();
  while (1U)
  {
    /* Receive new data */
    start = camera_encode_wait(&size);
    if (size == 0U)
    {
      continue;
    }
    end = start + size;

    /* Run send logic */
    header->timestamp = (uint16_t)(0xFFFF & xTaskGetTickCount());
    header->size      = (uint16_t)size;
    header->count     = ((size - 1U) / WIFI_LOSS_PAYLOAD_SIZE) + 1U;
    header->index     = 1U;
    while (start < end)
    {
      /* Prepare data */
      size = ((end - start) > WIFI_LOSS_PAYLOAD_SIZE)? WIFI_LOSS_PAYLOAD_SIZE : (end - start);
      memcpy(payload, start, size);
      start += size;

      /* Send and advance header */
      socket_sendto(socket, (uint8_t*)header, sizeof(t_loss_header) + size, 0, (const sockaddr_t*)&client, sizeof(client));
      header->index++;
    }

    /* Prepare next frame */
    camera_encode_request();
    header->frame++;
  }
}

#elif APP_SELECTION == APP_H264_UDP_STREAM
/**
 * @brief  Run the h264 UDP stream
 * @return 0 on success, negative value on error
 */
static int32_t w6x_h264_udp_stream(void)
{
  int32_t       socket;
  uint8_t       *start;
  uint8_t       *end;
  size_t        size;
  sockaddr_in_t client = {
    .sin_family     = AF_INET,
    .sin_port       = htons(RTP_PORT),
    .sin_addr.s_addr= htonl(REMOTE_IP),
  };

  /* Prepare */
  socket = w6x_socket_udp_create(0U, 0U);
  if (socket < 0)
  {
    LogError("Failed to create UDP socket, %" PRIi32 "\n", socket);
    return socket;
  }

  /* Stream */
  camera_encode_request();
  while (1U)
  {
    /* Receive new data */
    start = camera_encode_wait(&size);
    if (size == 0U)
    {
      continue;
    }
    end = start + size;

    /* Run send logic */
    while (start < end)
    {
      size = ((end - start) > UDP_PAYLOAD_MAX_SIZE)? UDP_PAYLOAD_MAX_SIZE : (end - start);
      socket_sendto(socket, start, size, 0, (const sockaddr_t*)&client, sizeof(client));
      start += size;
    }

    /* Prepare next frame */
    camera_encode_request();
  }
  return 0;
}

#elif APP_SELECTION == APP_H264_RTP_STREAM
/**
 * @brief  Run the h264 RTP stream
 * @return 0 on success, negative value on error
 */
static int32_t w6x_h264_rtp_stream()
{
  w6x_rtp_sender_t  sender    = { 0 };
  w6x_rtp_session_t session   = { 0 };

  uint32_t          timestamp = rand();
  encoded_frame_t   *frame    = NULL;
  uint32_t          ticks, prev;
  uint32_t          msw;
  uint32_t          lsw;
  int cnt =0;
  int dbg_log =0;
  uint32_t thp_kbps = 0;

  /* Create device RTP/RTCP sockets */
  w6x_rtp_sender_create(&sender, (uint8_t*)"RTP@ST67", RTP_PORT);

  /* Start a session with the remote player (hardcoded) */
  w6x_rtp_sender_session_create(&sender, &session, 96, REMOTE_RTP_PORT, REMOTE_RTCP_PORT, REMOTE_IP);

  vTaskDelay( pdMS_TO_TICKS(100) );
  /* Stream */
  camera_encode_request();
  while (1)
  {
    frame = camera_encode_wait();
    if (cnt ==1)
    {
      SCB_CleanInvalidateDCache();
    }
    if (frame && frame->data && frame->enc_size > 0)
    {
      ticks = frame->timestamp; // or your timestamp logic
      msw =  ticks / (configTICK_RATE_HZ);
      lsw = ((uint64_t)ticks << 32U) / configTICK_RATE_HZ;
      //uint32_t start= xTaskGetTickCount();

      w6x_rtp_sender_session_send_h264(&session, frame->data , frame->enc_size  , timestamp, msw, lsw, 1U);

      timestamp = ((uint64_t) ticks * camera.sensor.fps)/configTICK_RATE_HZ;
        // Optionally free frame.frame if needed
      camera_encode_free( );
      //uint32_t now= xTaskGetTickCount();

//      if ((now-start) !=0)
//       thp_kbps= frame->enc_size/(now-start);
//
//      LogDebug("%d:sent %d Bytes in %d ms, thp = %dkBps. 0x%08X ,delta %d ms\n",xTaskGetTickCount(), frame->enc_size, now-start, thp_kbps,  frame->data, ticks-prev);

      prev=ticks;
      cnt++;
    }
  }
  return 0;
}

#endif /* APP_SELECTION */

void EWLPoolChoiceCb(uint8_t **pool, size_t *size)
{

}

void EWLPoolReleaseCb(uint8_t **pool)
{

}

void BLE_Send_Wifi_Scan_Report_Notification(void)
{
  /* USER CODE BEGIN BLE_Send_Wifi_Scan_Report_Notification_1 */

  /* USER CODE END BLE_Send_Wifi_Scan_Report_Notification_1 */
  int32_t ret;
  uint32_t SentDatalen = 0;

  LogInfo("BLE Send Notification\n");
  ret = W6X_Ble_ServerSendNotification(WIFI_COMMISSIONING_SERVICE_INDEX, WIFI_AP_LIST_CHAR_INDEX,
                                       a_APP_WifiCommUpdateCharData, size_of_WifiCommUpdateCharData,
                                       &SentDatalen, 6000);

  if (ret)
  {
    LogError("Send Notification FAILED: %" PRIi32 "\n", ret);
  }
  return;
  /* USER CODE BEGIN BLE_Send_Wifi_Scan_Report_Notification_End */

  /* USER CODE END BLE_Send_Wifi_Scan_Report_Notification_End */
}

void BLE_Send_Wifi_Monitoring_Notification(void)
{
  int32_t ret;
  uint32_t SentDatalen = 0;
  /* USER CODE BEGIN BLE_Send_Wifi_Monitoring_Notification_1 */

  /* USER CODE END BLE_Send_Wifi_Monitoring_Notification_1 */

  LogInfo("BLE Send Notification\n");
  ret = W6X_Ble_ServerSendNotification(WIFI_COMMISSIONING_SERVICE_INDEX, WIFI_MONITORING_CHAR_INDEX,
                                       a_APP_WifiCommUpdateCharData, size_of_WifiCommUpdateCharData,
                                       &SentDatalen, 6000);

  if (ret)
  {
    LogError("Send Notification FAILED: %" PRIu32 "\n", SentDatalen);
  }
  return;
  /* USER CODE BEGIN BLE_Send_Wifi_Monitoring_Notification_End */

  /* USER CODE END BLE_Send_Wifi_Monitoring_Notification_End */
}

static void Run_Application(void){
	int32_t status = 0;

	LogInfo("\nChecking WiFi connection\n");
	if(sta_state == W6X_WIFI_STATE_STA_CONNECTED) {

		/* Run application */
		LogInfo("\nApplication is now running...\n");

		#if APP_SELECTION == APP_LWIP_TEST
		status = lwip_socket_test();
		#elif APP_SELECTION == APP_RTP_TEST
		status = w6x_rtp_test();
		#elif APP_SELECTION == APP_LOSS_DUMMY_TEST
		status = w6x_loss_dummy_test();
		#elif APP_SELECTION == APP_LOSS_H264_TEST
		status = w6x_loss_h264_test();
		#elif APP_SELECTION == APP_H264_UDP_STREAM
		status = w6x_h264_udp_stream();
		#elif APP_SELECTION == APP_H264_RTP_STREAM
		status = w6x_h264_rtp_stream();
		#else
		#error "Invalid APP_SELECTION value"
		#endif /* APP_SELECTION */
		if (status < 0)
		{
		  LogError("Application failed, %" PRIi32 "\n", status);
		}

		LogInfo("\nQuitting the application\n");
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  /* Callback when data is available in Network CoProcessor to enable SPI Clock */
  if (pin == SPI_RDY_Pin)
  {
    if (HAL_GPIO_ReadPin(SPI_RDY_GPIO_Port, SPI_RDY_Pin) == GPIO_PIN_SET)
    {
      HAL_GPIO_EXTI_Rising_Callback(pin);
    }
    else
    {
      HAL_GPIO_EXTI_Falling_Callback(pin);
    }
  }
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
  /* Callback when data is available in Network CoProcessor to enable SPI Clock */
  if (pin == SPI_RDY_Pin)
  {
    spi_on_txn_data_ready();
  }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
  /* Callback when data is available in Network CoProcessor to enable SPI Clock */
  if (pin == SPI_RDY_Pin)
  {
    spi_on_header_ack();
  }
}

