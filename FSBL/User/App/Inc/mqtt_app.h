#ifndef MQTT_APP_H
#define MQTT_APP_H

/**
 *******************************************************************************
 * @file    mqtt_app.h
 * @author  STMicroelectronics
 * @date    2026
 * @brief   MQTT module public API
 *******************************************************************************
 */

#include "main_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void APP_MQTT_Start(APP_Context_t *ctx);
void APP_MQTT_Task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_APP_H */
