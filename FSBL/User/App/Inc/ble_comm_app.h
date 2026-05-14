/**
 *******************************************************************************
 * @file    ble_comm_app.h
 * @author  STMicroelectronics
 * @date    2026
 * @brief   BLE Wi-Fi commissioning application helper:
 *          - BLE init / advertising / GATT commissioning service
 *          - BLE write handling for Wi-Fi credentials and control
 *          - BLE notifications for scan results and monitoring
 *******************************************************************************
 */

#ifndef BLE_COMM_APP_H
#define BLE_COMM_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "main_app.h"
#include "w6x_api.h"

#ifndef REDEFINE_FREERTOS_INTERFACE
#include "event_groups.h"
#endif /* REDEFINE_FREERTOS_INTERFACE */

/* BLE-related application events */
/** Event when BLE is connected */
#define EVT_APP_BLE_CONNECTED                       (1U << 1U)
/** Event when BLE is disconnected */
#define EVT_APP_BLE_DISCONNECTED                    (1U << 2U)
/** Event when BLE connection parameters are updated */
#define EVT_APP_BLE_CONNECTION_PARAM_UPDATE         (1U << 3U)
/** Event when BLE characteristic is read */
#define EVT_APP_BLE_READ                            (1U << 4U)
/** Event when BLE characteristic is written */
#define EVT_APP_BLE_WRITE                           (1U << 5U)
/** Event when BLE service is found */
#define EVT_APP_BLE_SERVICE_FOUND                   (1U << 6U)
/** Event when BLE characteristic is found */
#define EVT_APP_BLE_CHAR_FOUND                      (1U << 7U)
/** Event when BLE indication is enabled */
#define EVT_APP_BLE_INDICATION_STATUS_ENABLED       (1U << 8U)
/** Event when BLE indication is disabled */
#define EVT_APP_BLE_INDICATION_STATUS_DISABLED      (1U << 9U)
/** Event when BLE notification is enabled */
#define EVT_APP_BLE_NOTIFICATION_STATUS_ENABLED     (1U << 10U)
/** Event when BLE notification is disabled */
#define EVT_APP_BLE_NOTIFICATION_STATUS_DISABLED    (1U << 11U)
/** Event when BLE notification data is received */
#define EVT_APP_BLE_NOTIFICATION_DATA               (1U << 12U)
/** Event when BLE MTU size is updated */
#define EVT_APP_BLE_MTU_SIZE                        (1U << 13U)
/** Event when BLE pairing failed */
#define EVT_APP_BLE_PAIRING_FAILED                  (1U << 14U)
/** Event when BLE pairing is completed */
#define EVT_APP_BLE_PAIRING_COMPLETED               (1U << 15U)
/** Event when BLE pairing confirmation is requested */
#define EVT_APP_BLE_PAIRING_CONFIRM                 (1U << 16U)
/** Event when BLE passkey entry is requested */
#define EVT_APP_BLE_PASSKEY_ENTRY                   (1U << 17U)
/** Event when BLE passkey is displayed */
#define EVT_APP_BLE_PASSKEY_DISPLAYED               (1U << 18U)
/** Event when BLE passkey confirmation is requested */
#define EVT_APP_BLE_PASSKEY_CONFIRM                 (1U << 19U)
/** Event when BLE pairing is canceled */
#define EVT_APP_BLE_PAIRING_CANCELED                (1U << 20U)

/* Public API definitions ----------------------------------------------------*/

int32_t BLE_COMM_APP_Init(APP_Context_t *app_ctx, EventGroupHandle_t app_evt_current);

void BLE_COMM_APP_HandleBleEvent(W6X_event_id_t event_id, void *event_args);

void BLE_COMM_APP_ProcessButton(void);
void BLE_COMM_APP_ProcessNotificationEnabled(void);
void BLE_COMM_APP_ProcessWrite(void);
void BLE_COMM_APP_ProcessPasskeyEntry(void);
void BLE_COMM_APP_ProcessPasskeyConfirm(void);
void BLE_COMM_APP_ProcessPairingConfirm(void);
void BLE_COMM_APP_ProcessPairingCompleted(void);

void BLE_COMM_APP_NotifyWifiConnected(const W6X_WiFi_Connect_t *conn_data);
void BLE_COMM_APP_NotifyWifiConnectionTimeout(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_COMM_APP_H */
