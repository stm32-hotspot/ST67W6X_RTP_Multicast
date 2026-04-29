/**
 *******************************************************************************
 * @file    main_app.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Main application
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#ifndef _MAIN_APP_H_
#define _MAIN_APP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Public definitions --------------------------------------------------------*/

/* Public macros -------------------------------------------------------------*/

/* Public types --------------------------------------------------------------*/

/* Public data ---------------------------------------------------------------*/

/* Public API ----------------------------------------------------------------*/

/**
 * @brief Run the main application
 * @param args Task arguments
 */
void main_app(void* args);

/**
  * @brief  Send BLE notification for Wi-Fi scan characteristic
  */
void BLE_Send_Wifi_Scan_Report_Notification(void);

/**
  * @brief  Send BLE notification for Wi-Fi monitoring characteristic
  */
void BLE_Send_Wifi_Monitoring_Notification(void);

/*----------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _MAIN_APP_H_ */
