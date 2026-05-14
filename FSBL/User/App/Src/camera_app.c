/**
 *******************************************************************************
 * @file    camera_app.c
 * @author  STMicroelectronics
 * @date    2026
 * @brief   Camera application
 *******************************************************************************
 */
#include "camera_app.h"
#include "FreeRTOS.h"
#include "enccommon.h"
#include "event_groups.h"
#include "queue.h"
#include "ewl.h"
#include "h264encapi.h"
#include "isp_api.h"
#include "isp_param_conf.h"
#include "main.h"
#include "stm32n6570_discovery.h"
#include "stm32n6570_discovery_lcd.h"
#include "stm32n6xx_ll_venc.h"
#include "logging.h"
/* Private tunables ----------------------------------------------------------*/

#define CAMERA_FPS                30U

#define ENCODER_H264_SIZE         (512U * 1024U)
#define ENCODER_LEVEL             H264ENC_LEVEL_3_2
#define ENCODER_BITRATE           2000000U
#define ENCODER_QP                25U

#define APP_SELECTION             APP_STREAM_ST67

/* Private definitions -------------------------------------------------------*/

#define EVT_CAMERA_FRAME          (1U << 0U)
#define EVT_CAMERA_PREVIEW        (1U << 1U)
#define EVT_CAMERA_STREAM         (1U << 2U)
#define EVT_CAMERA_ENCODE_REQUEST (1U << 3U)
#define EVT_CAMERA_ENCODE_READY   (1U << 4U)

#define LTDC_WIDTH                RK050HR18_WIDTH
#define LTDC_HEIGHT               RK050HR18_HEIGHT

#define PREVIEW_PIPE              DCMIPP_PIPE2
#define PREVIEW_BPP               3U
#define PREVIEW_WIDTH             MIN(640U, LTDC_WIDTH)
#define PREVIEW_HEIGHT            MIN(480U, LTDC_HEIGHT)
#define PREVIEW_FORMAT            DCMIPP_PIXEL_PACKER_FORMAT_RGB888_YUV444_1
#define PREVIEW_FORMAT_LTDC       LTDC_PIXEL_FORMAT_RGB888

/* NOTE: Stream size is limited due VENC RAM requirements:
 * - Moving heap to PSRAM causes ST67 to stop working
 * - Heap was limited to 1MB. VGA requires 600KB on VENC
 */
#define STREAM_PIPE               DCMIPP_PIPE1
#define STREAM_BPP                2U
#define STREAM_WIDTH              1280U
#define STREAM_HEIGHT             720U
#define STREAM_FORMAT             DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1

/* App selection */
#define APP_STREAM_LTDC           1U
#define APP_STREAM_ST67           2U
#define APP_STREAM_ALL            3U

/* Private macros ------------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/** Encoder state */
typedef enum
{
  ENCODER_RESET = 0,
  ENCODER_INITIALIZED,
  ENCODER_STARTED,
} t_encoder_state;

/** Encoder instance */
typedef struct
{
  H264EncInst       instance;
  H264EncConfig     config;
  H264EncIn         input;
  H264EncOut        output;
  size_t            frame;
  uint8_t           forced;
  t_encoder_state   state;
} t_encoder;


/* Common ---------------------------*/
static EventGroupHandle_t _event;

/* Camera ---------------------------*/
t_camera                  camera = {
  .sensor = {
    .fps          = CAMERA_FPS,
    .mirror_flip  = CMW_MIRRORFLIP_NONE,
  },
  .preview = {      /* PREVIEW */
    .mode         = CMW_Aspect_ratio_fullscreen,
    .output_width = PREVIEW_WIDTH,
    .output_height= PREVIEW_HEIGHT,
    .output_format= PREVIEW_FORMAT,
    .output_bpp   = PREVIEW_BPP,
    .enable_swap  = 0,
    .enable_gamma_conversion = -1,
  },
  .stream = {      /* STREAM */
    .mode         = CMW_Aspect_ratio_crop,
    .output_width = STREAM_WIDTH,
    .output_height= STREAM_HEIGHT,
    .output_format= STREAM_FORMAT,
    .output_bpp   = STREAM_BPP,
    .enable_swap  = 0,
    .enable_gamma_conversion = -1,
  },
};
static uint8_t            _camera_preview[PREVIEW_BPP * PREVIEW_WIDTH * PREVIEW_HEIGHT] ALIGN32 IN_PSRAM;
static uint8_t            _camera_stream[2U][STREAM_BPP * STREAM_WIDTH * STREAM_HEIGHT] ALIGN32 IN_PSRAM;

/* Display --------------------------*/
static volatile uint8_t   _stop_encoder = 0U;
static uint32_t           _display_pitch = 0U;

/* Encoder --------------------------*/
static t_encoder          _encoder = { 0 };

#define ENCODER_FRAME_QUEUE_SIZE (5U)

static uint8_t            _encoder_stream[ENCODER_H264_SIZE] ALIGN32 IN_SRAM_UNCACHED;
static size_t             _encoder_stream_offset;

static volatile bool      _camera_stream_paused = false;
static volatile bool      _camera_started = false;
static volatile bool      _pause_request = false;
static volatile bool      _resume_request = false;

/* Private function ----------------------------------------------------------*/

/* Camera ---------------------------*/
static int32_t            _camera_init(void);
static uint8_t            *_camera_get_buffer(uint32_t pipe);
static int32_t            _camera_set_ipplug(uint32_t pow0, uint32_t pfs0, uint32_t pow1, uint32_t pfs1, uint32_t pow2, uint32_t pfs2);
static void               _camera_do_pause(void);
static void               _camera_do_resume(void);
extern int                CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe);
extern HAL_StatusTypeDef  MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp);

/* Encoder --------------------------*/
static int32_t            _encoder_init(void);
static int32_t            _encoder_process_frame(void);
static int32_t            _encoder_start(void);
static int32_t            _encoder_stop(void);
static void               _encoder_reset(void);
static uint8_t*           _get_next_frame_buffer(size_t *max_size);
static uint8_t            _encoder_queue_frame(encoded_frame_t *frame);

/* Public API definitions ----------------------------------------------------*/

static QueueHandle_t busy_buffer_queue = NULL;

/**
  * @brief  Main camera application task.
  * @param  args: Task argument, unused.
  * @retval None
  * @note   Initializes camera, optional display, and encoder resources, then
  *         runs the main frame-processing loop. The loop handles pause/resume
  *         requests, services camera events, and triggers frame encoding when
  *         requested.
  */
void camera_app(void* args)
{
  int32_t status;

  /* Initialize */
  _encoder_stream_offset = 0;
  busy_buffer_queue = xQueueCreate(ENCODER_FRAME_QUEUE_SIZE, sizeof(encoded_frame_t));

  /* Initialize */
  _event = xEventGroupCreate();
  if (_event == NULL)
  {
    Error_Handler();
  }
  status = _camera_init();
  if (status != BSP_ERROR_NONE)
  {
    Error_Handler();
  }
  #if APP_SELECTION & APP_STREAM_LTDC
  status = _display_init();
  if (status != BSP_ERROR_NONE)
  {
    Error_Handler();
  }
  #endif
  #if APP_SELECTION & APP_STREAM_ST67
  status = _encoder_init();
  if (status != BSP_ERROR_NONE)
  {
    Error_Handler();
  }
  #endif

  /* Run the camera logic (every frame) */
  while (1U)
  {
    if (_pause_request)
    {
      _pause_request = false;
      _camera_do_pause();
    }

    if (_resume_request)
    {
      _resume_request = false;
      _camera_do_resume();
    }

    xEventGroupWaitBits(_event, EVT_CAMERA_FRAME, pdTRUE, pdFALSE, portMAX_DELAY);

    if (_pause_request)
    {
      _pause_request = false;
      _camera_do_pause();
    }

    if (_resume_request)
    {
      _resume_request = false;
      _camera_do_resume();
    }

    /* IMPORTANT:
     * Even when paused, keep servicing the camera pipeline.
     * Stopping CMW_CAMERA_Run() while frames/interrupts are still active
     * may stall the capture path and freeze the application.
     */
    status = CMW_CAMERA_Run();
    if (status != CMW_ERROR_NONE)
    {
      /* Print error info */
    }

    #if APP_SELECTION == APP_STREAM_ST67
    /* VENC: Process frame */
    if (_camera_stream_paused)
    {
      continue;
    }

    status = xEventGroupWaitBits(_event, EVT_CAMERA_ENCODE_REQUEST, pdFALSE, pdFALSE, 0);

    if (_pause_request)
    {
      _pause_request = false;
      _camera_do_pause();
      continue;
    }

    if (_resume_request)
    {
      _resume_request = false;
      _camera_do_resume();
    }

    if (_camera_stream_paused)
    {
      continue;
    }

    if (status & EVT_CAMERA_ENCODE_REQUEST)
    {
      /* Encoder: Process frame */
      status = _encoder_process_frame();
      if (status != BSP_ERROR_NONE)
      {
        /* Print error info */
        BSP_LED_Toggle(LED_RED);
      }
    }
    #endif /* APP_SELECTION */
  }
}

/**
  * @brief  Waits for the next available camera buffer of the requested type.
  * @param  buffer: Camera buffer type to wait for.
  * @param  size: Pointer updated with the returned buffer size in bytes.
  * @retval Pointer to the selected camera buffer, or NULL if the buffer type is invalid.
  * @note   This function blocks until the corresponding frame event is received.
  */
uint8_t *camera_wait_buffer(t_camera_buffer buffer, size_t *size)
{
  switch (buffer)
  {
    case CAMERA_PREVIEW:
      /* Wait for preview frame */
      xEventGroupWaitBits(_event, EVT_CAMERA_PREVIEW, pdTRUE, pdFALSE, portMAX_DELAY);
      *size = PREVIEW_BPP * PREVIEW_WIDTH * PREVIEW_HEIGHT;
      return _camera_get_buffer(PREVIEW_PIPE);

    case CAMERA_STREAM:
      /* Wait for stream frame */
      xEventGroupWaitBits(_event, EVT_CAMERA_STREAM, pdTRUE, pdFALSE, portMAX_DELAY);
      *size = STREAM_BPP * STREAM_WIDTH * STREAM_HEIGHT;
      return _camera_get_buffer(STREAM_PIPE);

    default:
      /* Invalid buffer */
      *size = 0;
      return NULL;
  }
}

/**
  * @brief  Requests encoding of the next available camera stream frame.
  * @param  None
  * @retval None
  * @note   Sets the encoder request event bit if the event group is initialized.
  */
void camera_encode_request(void)
{
  if (_event != NULL)
  {
    xEventGroupSetBits(_event, EVT_CAMERA_ENCODE_REQUEST);
  }
}

/**
  * @brief  Waits for an encoded frame to become available in the queue.
  * @param  None
  * @retval Pointer to a static copy of the encoded frame descriptor, or NULL if no frame is available.
  * @note   Uses a finite timeout so the caller can periodically re-evaluate
  *         pause/resume conditions.
  */
encoded_frame_t *camera_encode_wait(void)
{
  /* Get a copy of the frame from the queue.
   * Use a finite timeout so the RTP task can re-check pause/resume requests.
   */
  static encoded_frame_t frame;

  if (busy_buffer_queue == NULL)
  {
    return NULL;
  }

  if (xQueuePeek(busy_buffer_queue, &frame, pdMS_TO_TICKS(50U)) == pdTRUE)
  {
    return &frame;
  }

  return NULL;
}

/* Private function definitions ----------------------------------------------*/

/* Camera ---------------------------*/
/**
  * @brief  Initializes the camera sensor and both capture pipes.
  * @param  None
  * @retval BSP status code.
  * @note   Configures sensor exposure mode, stream and preview pipes, IPPlug
  *         bandwidth distribution, and starts continuous acquisition.
  */
int32_t _camera_init(void)
{
  uint32_t  pitch = 0;
  int32_t   status;

  /* Initialize the camera */
  status = CMW_CAMERA_Init(&camera.sensor);
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }

  status = CMW_CAMERA_SetExposureMode(CMW_EXPOSUREMODE_MANUAL);
  if ((status != CMW_ERROR_NONE) && (status != CMW_ERROR_FEATURE_NOT_SUPPORTED))
  {
    return status;
  }

  /* Configure pipes */
  status = CMW_CAMERA_SetPipeConfig(STREAM_PIPE, &camera.stream, &pitch);
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }
  status = CMW_CAMERA_SetPipeConfig(PREVIEW_PIPE, &camera.preview, &_display_pitch);
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }

  /* Configure IPPlug */
  status = _camera_set_ipplug(
    0, 0,
    camera.stream.output_width, camera.stream.output_bpp,
    camera.preview.output_width, camera.preview.output_bpp
  );
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }

  /* Start pipes */
  status = CMW_CAMERA_DoubleBufferStart(STREAM_PIPE, (uint8_t*)_camera_stream[0], (uint8_t*)_camera_stream[1], CMW_MODE_CONTINUOUS);
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }
  status = CMW_CAMERA_Start(PREVIEW_PIPE, (uint8_t*)_camera_preview, CMW_MODE_CONTINUOUS);
  if (status != CMW_ERROR_NONE)
  {
    return status;
  }

  _camera_started = true;
  _camera_stream_paused = false;

  return BSP_ERROR_NONE;
}

/**
  * @brief  Returns the current memory buffer address associated with a camera pipe.
  * @param  pipe: DCMIPP pipe identifier.
  * @retval Pointer to the active pipe buffer, or NULL for an unsupported pipe.
  */
static uint8_t *_camera_get_buffer(uint32_t pipe)
{
  switch (pipe)
  {
    case DCMIPP_PIPE1: return (uint8_t*)DCMIPP->P1STM0AR;
    case DCMIPP_PIPE2: return (uint8_t*)DCMIPP->P2STM0AR;
    default:           return NULL;
  }
}

/**
  * @brief  Configures DCMIPP IPPlug arbitration based on pipe bandwidth usage.
  * @param  pow0: Width-related contribution for pipe 0.
  * @param  pfs0: Bytes-per-pixel contribution for pipe 0.
  * @param  pow1: Width-related contribution for pipe 1.
  * @param  pfs1: Bytes-per-pixel contribution for pipe 1.
  * @param  pow2: Width-related contribution for pipe 2.
  * @param  pfs2: Bytes-per-pixel contribution for pipe 2.
  * @retval ISP status code.
  * @note   Computes proportional bandwidth and outstanding transaction values
  *         for each DCMIPP client.
  */
static int32_t _camera_set_ipplug(uint32_t pow0, uint32_t pfs0, uint32_t pow1, uint32_t pfs1, uint32_t pow2, uint32_t pfs2)
{
  uint32_t client[DCMIPP_NUM_OF_PIPES]= {DCMIPP_CLIENT1, DCMIPP_CLIENT2, DCMIPP_CLIENT5}; /* AXI master client for Dump Pipe, Main Pipe in RGB and Ancillary pipe */
  uint32_t PBP[DCMIPP_NUM_OF_PIPES]   = {pow0 * pfs0, pow1 * pfs1, pow2 * pfs2};          /* Peak Bandwidth proportion for each pipe */
  uint32_t PBP_ALL = PBP[0] + PBP[1] + PBP[2];
  DCMIPP_IPPlugConfTypeDef config = {0};

  config.MemoryPageSize = DCMIPP_MEMORY_PAGE_SIZE_256BYTES;
  config.Traffic        = DCMIPP_TRAFFIC_BURST_SIZE_128BYTES;
  config.WLRURatio      = 10;

  /* Configure IPPlug for each pipe */
  for (size_t idx = 0; idx < DCMIPP_NUM_OF_PIPES; idx++)
  {
    config.Client     = client[idx];
    config.DPREGStart = config.DPREGEnd ? config.DPREGEnd + 1 : 0;
    config.DPREGEnd   = MIN((config.DPREGStart + 640 * PBP[idx] / PBP_ALL), 639);
    config.MaxOutstandingTransactions = ((16 * PBP[idx] / PBP_ALL) >= 1) ? (16 * PBP[idx] / PBP_ALL) - 1 : 0;
    if (HAL_DCMIPP_SetIPPlugConfig(CMW_CAMERA_GetDCMIPPHandle(), &config) != HAL_OK)
    {
      return ISP_ERR_DCMIPP_CONFIGPIPE;
    }
  }
  return ISP_OK;
}

/**
  * @brief  Applies the internal pause state for the camera streaming/encoding path.
  * @param  None
  * @retval None
  * @note   Clears pending encode requests and marks the stream path as paused
  *         without fully stopping frame servicing.
  */
static void _camera_do_pause(void)
{
  if (_event != NULL)
  {
    /* Stop new encode requests while paused */
    xEventGroupClearBits(_event, EVT_CAMERA_ENCODE_REQUEST);
  }

  _camera_stream_paused = true;
  _camera_started = false;

  LogInfo("Camera encode path paused\n");
}

/**
  * @brief  Resumes the internal camera streaming/encoding path.
  * @param  None
  * @retval None
  * @note   Forces the next encoded frame to be intra-coded and re-arms the
  *         encoder request event.
  */
static void _camera_do_resume(void)
{
  /* Force an intra frame after resume */
  _encoder.forced = 1U;

  _camera_stream_paused = false;
  _camera_started = true;

  if (_event != NULL)
  {
    xEventGroupClearBits(_event, EVT_CAMERA_ENCODE_REQUEST);
    xEventGroupSetBits(_event, EVT_CAMERA_ENCODE_REQUEST);
  }

  LogInfo("Camera encode path resumed\n");
}

/**
  * @brief  Camera frame event callback executed on pipe frame completion.
  * @param  pipe: Pipe identifier that generated the frame event.
  * @retval HAL_OK
  * @note   Posts preview or stream-related event bits from ISR context and
  *         triggers a context switch if required.
  */
int CMW_CAMERA_PIPE_FrameEventCallback(uint32_t pipe)
{
  BaseType_t  woken = pdFALSE;
  int32_t     status;

  switch (pipe)
  {
    case PREVIEW_PIPE:
      status = xEventGroupSetBitsFromISR(_event, EVT_CAMERA_PREVIEW, &woken);
      break;

    case STREAM_PIPE:
      status = xEventGroupSetBitsFromISR(_event, EVT_CAMERA_FRAME | EVT_CAMERA_STREAM, &woken);
      break;

    default:
      status = pdFAIL;
      break;
  }

  if (status == pdPASS)
  {
    portYIELD_FROM_ISR(woken);
  }

  return HAL_OK;
}

/**
  * @brief  Configures peripheral clocks required by DCMIPP and CSI.
  * @param  hdcmipp: Pointer to DCMIPP handle, unused.
  * @retval HAL status.
  * @note   Selects and divides internal clock sources for DCMIPP and CSI.
  */
HAL_StatusTypeDef MX_DCMIPP_ClockConfig(DCMIPP_HandleTypeDef *hdcmipp)
{
  RCC_PeriphCLKInitTypeDef  config = { 0 };
  int32_t                   status;

  UNUSED(hdcmipp);

  /* Configure DCMIPP/CSI clocks
   * DCMIPP = IC17 = PLL2 / 3  = 333MHz
   * CSI    = IC18 = PLL1 / 40 = 20MHz
   */
  config.PeriphClockSelection = RCC_PERIPHCLK_DCMIPP | RCC_PERIPHCLK_CSI;
  config.DcmippClockSelection = RCC_DCMIPPCLKSOURCE_IC17;
  config.ICSelection[RCC_IC17].ClockSelection = RCC_ICCLKSOURCE_PLL2;
  config.ICSelection[RCC_IC17].ClockDivider   = 3;
  config.ICSelection[RCC_IC18].ClockSelection = RCC_ICCLKSOURCE_PLL4;
  config.ICSelection[RCC_IC18].ClockDivider   = 40;
  status = HAL_RCCEx_PeriphCLKConfig(&config);
  if (status != HAL_OK)
  {
    return HAL_ERROR;
  }
  return HAL_OK;
}

/* Encoder --------------------------*/
/**
  * @brief  Initializes the H.264 encoder instance and runtime parameters.
  * @param  None
  * @retval BSP status code.
  * @note   Configures encoder profile level, preprocessing format, coding
  *         control, and rate control parameters for the camera stream.
  */
static int32_t _encoder_init(void)
{
  H264EncCodingCtrl       coding  = { 0 };
  H264EncPreProcessingCfg preproc = { 0 };
  H264EncRateCtrl         rate    = { 0 };
  int32_t                 status;

  /* Validate */
  if (_encoder.state > ENCODER_RESET)
  {
    return BSP_ERROR_NONE;
  }

  /* initialize VENC */
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  LL_VENC_Init();

  /* Configure
   * - H.264 encoder (reduced bitrate)
   * - Prepare for DCMIPP stream pipe encoding
   */
  _encoder.config.level          = ENCODER_LEVEL;
  _encoder.config.streamType     = H264ENC_BYTE_STREAM;
  _encoder.config.viewMode       = H264ENC_BASE_VIEW_SINGLE_BUFFER;
  _encoder.config.width          = camera.stream.output_width;
  _encoder.config.height         = camera.stream.output_height;
  _encoder.config.frameRateNum   = camera.sensor.fps;
  _encoder.config.frameRateDenom = 1;
  _encoder.config.refFrameAmount = 1;
  status = H264EncInit(&_encoder.config, &_encoder.instance);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Setup coding control */
  status = H264EncGetCodingCtrl(_encoder.instance, &coding);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  coding.idrHeader = 1;
  status = H264EncSetCodingCtrl(_encoder.instance, &coding);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Setup source format: RGB565 */
  status = H264EncGetPreProcessing(_encoder.instance, &preproc);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  preproc.inputType = H264ENC_RGB565;
  status = H264EncSetPreProcessing(_encoder.instance, &preproc);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Setup rate control */
  status = H264EncGetRateCtrl(_encoder.instance, &rate);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }
  rate.pictureRc    = 1;
  rate.mbRc         = 1;
  rate.pictureSkip  = 0;
  rate.hrd          = 0;
  rate.qpHdr        = ENCODER_QP;
  rate.qpMin        = 10;
  rate.qpMax        = 51;
  rate.gopLen       = camera.sensor.fps;
  rate.bitPerSecond = ENCODER_BITRATE;
  rate.intraQpDelta = 0;
  status = H264EncSetRateCtrl(_encoder.instance, &rate);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Update state */
  _encoder.state = ENCODER_INITIALIZED;
  return BSP_ERROR_NONE;
}

/**
  * @brief  Encodes one camera stream frame into the output bitstream buffer.
  * @param  None
  * @retval BSP status code.
  * @note   Starts the encoder on first use, prepares input/output buffers,
  *         selects frame type, runs encoding, and queues the resulting frame.
  */
static int32_t _encoder_process_frame(void)
{
  int32_t status;
  encoded_frame_t frame;

  /* Validate */
  if (_encoder.state < ENCODER_INITIALIZED)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Start encoder if needed (skip a frame) */
  if (_encoder.state == ENCODER_INITIALIZED)
  {
    return _encoder_start();
  }

  frame.data = _get_next_frame_buffer(&frame.max_size);
  if (frame.data == NULL)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Prepare buffers */
  _encoder.input.busLuma       = (ptr_t)_camera_get_buffer(STREAM_PIPE);
  _encoder.input.pOutBuf       = (u32*)frame.data;
  _encoder.input.busOutBuf     = (ptr_t)frame.data;
  _encoder.input.outBufSize    = (u32)frame.max_size;

  /* Prepare frame type */
  _encoder.forced              = _encoder.forced | ((_encoder.frame % camera.sensor.fps) == 0U);
  _encoder.input.codingType    = _encoder.forced ? H264ENC_INTRA_FRAME : H264ENC_PREDICTED_FRAME;
  _encoder.input.ipf           = H264ENC_REFERENCE_AND_REFRESH;
  _encoder.input.ltrf          = H264ENC_NO_REFERENCE_NO_REFRESH;
  _encoder.input.timeIncrement = 1U;
  _encoder.forced              = 0U;

  /* Run encoder process */
  status = H264EncStrmEncode(_encoder.instance, &_encoder.input, &_encoder.output, NULL, NULL, NULL);
  frame.enc_size = _encoder.output.streamSize;
  frame.timestamp = xTaskGetTickCount();

  switch (status)
  {
    case H264ENC_FRAME_READY:
      /* Frame ready: Save stream */
      if (_encoder.output.streamSize == 0U)
      {
        /* No data: Force intra frame */
        _encoder.forced = 1U;
        return BSP_ERROR_COMPONENT_FAILURE;
      }

      /* Report ready and continue */
      _encoder.input.codingType = H264ENC_PREDICTED_FRAME;
      xEventGroupSetBits(_event, EVT_CAMERA_ENCODE_READY);
      if (_encoder_queue_frame(&frame) != BSP_ERROR_NONE)
      {
        return BSP_ERROR_BUSY;
      }
      break;

    case H264ENC_FUSE_ERROR:
      /* Error: DCMIPP/VENC desync > Restart */
      _encoder_reset();
      break;

    default:
      /* Error: ANY > Force intra */
      _encoder.forced = 1U;
      return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Update frame index */
  _encoder.frame++;
  return BSP_ERROR_NONE;
}

/**
  * @brief  Starts the H.264 stream and produces the initial stream header data.
  * @param  None
  * @retval BSP status code.
  * @note   Allocates the next output buffer, starts the bitstream, updates the
  *         encoder state, and queues the produced header/frame payload.
  */
static int32_t _encoder_start(void)
{
  int32_t status;
  encoded_frame_t frame;

  frame.data = _get_next_frame_buffer(&frame.max_size);

  /* Validate */
  if (_encoder.state < ENCODER_INITIALIZED)
  {
    return BSP_ERROR_NO_INIT;
  }
  else if (_encoder.state == ENCODER_STARTED)
  {
    return BSP_ERROR_NONE;
  }

  if (frame.data == NULL)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  /* Configure output buffer (fixed) */
  _encoder.input.pOutBuf    = (u32*)frame.data;
  _encoder.input.busOutBuf  = (ptr_t)frame.data;
  _encoder.input.outBufSize = (u32)frame.max_size;

  /* Start the encoder */
  status = H264EncStrmStart(_encoder.instance, &_encoder.input, &_encoder.output);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  frame.enc_size = _encoder.output.streamSize;
  frame.timestamp = xTaskGetTickCount();

  /* Update state */
  _encoder.state = ENCODER_STARTED;

  return _encoder_queue_frame(&frame);
}

/**
  * @brief  Stops the H.264 stream encoder.
  * @param  None
  * @retval BSP status code.
  * @note   Ends the current encoded stream and transitions the encoder back to
  *         the initialized state.
  */
static int32_t _encoder_stop(void)
{
  int32_t status;

  /* Validate */
  if (_encoder.state < ENCODER_INITIALIZED)
  {
    return BSP_ERROR_NO_INIT;
  }
  else if (_encoder.state == ENCODER_INITIALIZED)
  {
    return BSP_ERROR_NONE;
  }

  /* Stop the encoder */
  status = H264EncStrmEnd(_encoder.instance, &_encoder.input, &_encoder.output);
  if (status != H264ENC_OK)
  {
    return BSP_ERROR_COMPONENT_FAILURE;
  }

  _encoder.state = ENCODER_INITIALIZED;
  return BSP_ERROR_NONE;
}

/**
  * @brief  Resets the VENC hardware block.
  * @param  None
  * @retval None
  * @note   Performs a hardware reset sequence with short delays to recover
  *         from encoder/pipe synchronization failures.
  */
static void _encoder_reset(void)
{
  /* Validate */
  if (_encoder.state < ENCODER_INITIALIZED)
  {
    return;
  }

  /* Reset the encoder */
  __HAL_RCC_VENC_FORCE_RESET();
  vTaskDelay(pdMS_TO_TICKS(5U));
  __HAL_RCC_VENC_RELEASE_RESET();
  vTaskDelay(pdMS_TO_TICKS(5U));
}

/**
  * @brief  Returns the next free position in the encoded bitstream buffer.
  * @param  max_size: Pointer updated with the remaining writable buffer size.
  * @retval Pointer to the next output location, or NULL if no space remains.
  */
static uint8_t* _get_next_frame_buffer(size_t *max_size)
{
  if (_encoder_stream_offset > ENCODER_H264_SIZE)
  {
    *max_size = 0;
    return NULL;
  }

  *max_size = ENCODER_H264_SIZE - _encoder_stream_offset;
  return _encoder_stream + _encoder_stream_offset;
}

/**
  * @brief  Queues an encoded frame descriptor for downstream consumption.
  * @param  frame: Pointer to the encoded frame descriptor to enqueue.
  * @retval BSP status code.
  * @note   On success, advances the shared stream buffer offset and aligns it
  *         to the next 32-byte boundary.
  */
static uint8_t _encoder_queue_frame(encoded_frame_t *frame)
{
  /* Copy the frame struct into the queue (not pointer) */
  if (xQueueSend(busy_buffer_queue, frame, 0) == pdTRUE)
  {
    taskENTER_CRITICAL();
    _encoder_stream_offset += frame->enc_size;
    taskEXIT_CRITICAL();

    /* Align _encoder_stream_offset to next 32-byte boundary */
    _encoder_stream_offset = (_encoder_stream_offset + 31U) & ~31U;

    return BSP_ERROR_NONE;
  }
  else
  {
    return BSP_ERROR_BUSY;
  }
}

/**
  * @brief  Releases the oldest encoded frame from the queue.
  * @param  None
  * @retval None
  * @note   If the queue becomes empty after removal, the encoded stream buffer
  *         offset is reset to allow reuse from the beginning.
  */
void camera_encode_free(void)
{
  encoded_frame_t frame;

  /* Remove the frame from the queue (copy out, not pointer) */
  if (xQueueReceive(busy_buffer_queue, &frame, 0) == pdTRUE)
  {
    /* Free the buffer (if needed) */
  }

  taskENTER_CRITICAL();
  /* If queue is now empty, reset offset */
  if (uxQueueMessagesWaiting(busy_buffer_queue) == 0U)
  {
    _encoder_stream_offset = 0U;
  }
  taskEXIT_CRITICAL();
}

/**
  * @brief  Requests pause of the camera streaming/encoding path.
  * @param  None
  * @retval BSP_ERROR_NONE
  * @note   The actual pause is handled asynchronously by the camera task loop.
  */
int32_t camera_stream_pause(void)
{
  _pause_request = true;
  return BSP_ERROR_NONE;
}

/**
  * @brief  Requests resume of the camera streaming/encoding path.
  * @param  None
  * @retval BSP_ERROR_NONE
  * @note   The actual resume is handled asynchronously by the camera task loop.
  */
int32_t camera_stream_resume(void)
{
  _resume_request = true;
  return BSP_ERROR_NONE;
}

/**
  * @brief  Indicates whether the camera stream path is currently paused.
  * @param  None
  * @retval true if paused, false otherwise.
  */
bool camera_stream_is_paused(void)
{
  return _camera_stream_paused;
}

/**
  * @brief  Flushes all pending encoded frames from the queue.
  * @param  None
  * @retval None
  * @note   Iteratively frees queued encoded frames until the queue is empty.
  */
void camera_encode_flush(void)
{
  while ((busy_buffer_queue != NULL) && (uxQueueMessagesWaiting(busy_buffer_queue) > 0U))
  {
    camera_encode_free();
  }
}
