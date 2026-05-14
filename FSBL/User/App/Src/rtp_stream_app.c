/**
 *******************************************************************************
 * @file    rtp_stream_app.c
 * @author  STMicroelectronics
 * @date    2026
 * @brief   H264 RTP streaming module
 *******************************************************************************
 */

#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>

#include "rtp_stream_app.h"
#include "camera_app.h"
#include "logging.h"
#include "main.h"
#include "w6x_rtp_sender.h"
#include "splash_frame.h"


/* Private tunables ----------------------------------------------------------*/
//#define REMOTE_IP                 0xC0A808C6UL  /* 192.168.8.198 Unicast */
#define REMOTE_IP                   0xEF000000UL  /* 239.0.0.0 Multicast */
//#define REMOTE_IP                 0xFFFFFFFFUL  /* 255.255.255.255 Broadcast */

#define REMOTE_RTP_PORT             RTP_PORT
#define REMOTE_RTCP_PORT            (RTP_PORT + 1U)

#define STREAM_THREAD_PRIO          24
#define STREAM_TASK_STACK_SIZE      4096U

/* Private function prototypes -----------------------------------------------*/

static int32_t w6x_h264_rtp_stream(APP_Context_t *ctx);

/* Private function definitions ----------------------------------------------*/

/**
  * @brief  Streams H.264 encoded frames over RTP.
  * @param  ctx: Pointer to the application context.
  * @retval Status code.
  * @note   Initializes the RTP sender/session, sends splash frames while the
  *         stream is paused, and forwards encoded camera frames when active.
  *         The function runs continuously until the task is terminated.
  */
static int32_t w6x_h264_rtp_stream(APP_Context_t *ctx)
{
  w6x_rtp_sender_t sender = {0};
  w6x_rtp_session_t session = {0};

  uint32_t timestamp = rand();
  encoded_frame_t *frame = NULL;
  uint32_t ticks;
  uint32_t msw;
  uint32_t lsw;
  int cnt = 0;

  w6x_rtp_sender_create(&sender, (uint8_t *)"RTP@ST67", RTP_PORT);
  w6x_rtp_sender_session_create(&sender, &session, 96, REMOTE_RTP_PORT, REMOTE_RTCP_PORT, REMOTE_IP);

  if (!SplashFrame_IsReady())
  {
    SplashFrame_Init();
  }

  vTaskDelay(pdMS_TO_TICKS(100U));
  camera_encode_request();

  while (1)
  {
    if (ctx->rtp_stream_paused)
    {
      static TickType_t last_splash_tick = 0U;
      TickType_t now = xTaskGetTickCount();

      /* Drain already queued encoded frames while paused so resume starts fresh */
      camera_encode_flush();

      /* Send splash frame at a low fixed rate while paused */
      if ((now - last_splash_tick) >= pdMS_TO_TICKS(200U))
      {
        msw = now / configTICK_RATE_HZ;
        lsw = ((uint64_t)now << 32U) / configTICK_RATE_HZ;

        xEventGroupSetBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_TX_BUSY);

        w6x_rtp_sender_session_send_h264(&session,
                                         splash_frame,
                                         splash_frame_len,
                                         timestamp,
                                         msw,
                                         lsw,
                                         1U);

        xEventGroupClearBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_TX_BUSY);

        timestamp += 90000U / 5U; /* 5 fps splash loop */
        last_splash_tick = now;
      }

      vTaskDelay(pdMS_TO_TICKS(20U));
      continue;
    }

    frame = camera_encode_wait();
    if (cnt == 1)
    {
      SCB_CleanInvalidateDCache();
    }

    if ((frame != NULL) && (frame->data != NULL) && (frame->enc_size > 0U))
    {
      if (ctx->rtp_stream_paused)
      {
        camera_encode_flush();
        vTaskDelay(pdMS_TO_TICKS(10U));
        continue;
      }

      ticks = frame->timestamp;
      msw = ticks / (configTICK_RATE_HZ);
      lsw = ((uint64_t)ticks << 32U) / configTICK_RATE_HZ;

      xEventGroupSetBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_TX_BUSY);

      w6x_rtp_sender_session_send_h264(&session,
                                       frame->data,
                                       frame->enc_size,
                                       timestamp,
                                       msw,
                                       lsw,
                                       1U);

      if (frame->enc_size > 20000U)
      {
        ctx->mqtt_publish_postpone_until = xTaskGetTickCount() + pdMS_TO_TICKS(200U);
      }

      xEventGroupClearBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_TX_BUSY);

      timestamp = ((uint64_t)ticks * camera.sensor.fps) / configTICK_RATE_HZ;
      camera_encode_free();

      if (!ctx->rtp_stream_paused)
      {
        camera_encode_request();
      }

      cnt++;
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(5U));
    }
  }

  return 0;
}

/* Public API definitions ----------------------------------------------------*/

/**
  * @brief  Entry point of the RTP stream FreeRTOS task.
  * @param  arg: Pointer to the application context.
  * @retval None
  * @note   Verifies the context and Wi-Fi connection state, marks the stream
  *         as active, runs the RTP streaming loop, and deletes the task on exit.
  */
void APP_RTP_Stream_Task(void *arg)
{
  APP_Context_t *ctx = (APP_Context_t *)arg;
  int32_t status = 0;

  if (ctx == NULL)
  {
    vTaskDelete(NULL);
    return;
  }

  LogInfo("\nChecking WiFi connection\n");
  if (ctx->sta_state == W6X_WIFI_STATE_STA_CONNECTED)
  {
    LogInfo("\nApplication is now running...\n");

    xEventGroupSetBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_ACTIVE);
    status = w6x_h264_rtp_stream(ctx);
    xEventGroupClearBits(ctx->app_runtime_flags, EVENT_FLAG_STREAM_ACTIVE);

    if (status < 0)
    {
      LogError("Application failed, %" PRIi32 "\n", status);
    }

    LogInfo("\nQuitting the application\n");
  }

  ctx->stream_task_handle = NULL;
  vTaskDelete(NULL);
}

/**
  * @brief  Starts the RTP stream task if it is not already running.
  * @param  ctx: Pointer to the application context.
  * @retval None
  * @note   Creates the FreeRTOS task responsible for RTP streaming and stores
  *         its handle in the application context.
  */
void APP_RTP_Stream_Start(APP_Context_t *ctx)
{
  if (ctx == NULL)
  {
    return;
  }

  if (ctx->stream_task_handle != NULL)
  {
    LogInfo("RTP stream task already running\n");
    return;
  }

  if (xTaskCreate(APP_RTP_Stream_Task,
                  "stream",
                  STREAM_TASK_STACK_SIZE >> 2U,
                  ctx,
                  STREAM_THREAD_PRIO,
                  &ctx->stream_task_handle) != pdPASS)
  {
    LogError("Failed to create stream task\n");
    ctx->stream_task_handle = NULL;
  }
}

/**
  * @brief  Toggles the RTP stream paused state.
  * @param  ctx: Pointer to the application context.
  * @retval None
  * @note   When pausing, it requests pause of the camera encode path. When
  *         resuming, it requests camera stream resume before clearing the
  *         paused state flag.
  */
void APP_RTP_Stream_TogglePause(APP_Context_t *ctx)
{
  if (ctx == NULL)
  {
    return;
  }

  if (!ctx->rtp_stream_paused)
  {
    ctx->rtp_stream_paused = true;
    (void)camera_stream_pause();
    LogInfo("RTP pause requested\n");
  }
  else
  {
    (void)camera_stream_resume();
    ctx->rtp_stream_paused = false;
    LogInfo("RTP resume requested\n");
  }
}

/**
  * @brief  Returns the current pause state of the RTP stream.
  * @param  ctx: Pointer to the application context.
  * @retval true if the RTP stream is paused, false otherwise.
  */
bool APP_RTP_Stream_IsPaused(APP_Context_t *ctx)
{
  if (ctx == NULL)
  {
    return false;
  }

  return ctx->rtp_stream_paused;
}
