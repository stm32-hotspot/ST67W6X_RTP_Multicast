#ifndef RTP_STREAM_APP_H
#define RTP_STREAM_APP_H

/**
 *******************************************************************************
 * @file    rtp_stream_app.h
 * @author  STMicroelectronics
 * @date    2026
 * @brief   RTP streaming module public API
 *******************************************************************************
 */

#include "main_app.h"

#ifdef __cplusplus
extern "C" {
#endif

void APP_RTP_Stream_Start(APP_Context_t *ctx);
void APP_RTP_Stream_Task(void *arg);
void APP_RTP_Stream_TogglePause(APP_Context_t *ctx);
bool APP_RTP_Stream_IsPaused(APP_Context_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* RTP_STREAM_APP_H */
