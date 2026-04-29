/**
 *******************************************************************************
 * @file		camera_app.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Camera application
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#ifndef _CAMERA_APP_H_
#define _CAMERA_APP_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "cmw_camera.h"

/* Public definitions --------------------------------------------------------*/

/* Public macros -------------------------------------------------------------*/

/* Public types --------------------------------------------------------------*/

/** Available camera pipes */
typedef enum
{
  CAMERA_PREVIEW = 0U,
  CAMERA_STREAM,
} t_camera_buffer;

/** Camera configuration */
typedef struct
{
  CMW_CameraInit_t  sensor;
  CMW_DCMIPP_Conf_t preview;
  CMW_DCMIPP_Conf_t stream;
} t_camera;

typedef struct {
    uint8_t *data;
    size_t   max_size;
    size_t   enc_size;
    uint32_t  timestamp;
} encoded_frame_t;

/* Public data ---------------------------------------------------------------*/

extern t_camera camera;     /*!< Camera configuration */

/* Public API ----------------------------------------------------------------*/

/**
 * @brief Run the camera application
 * @param args Task arguments
 */
void    camera_app(void* args);

void    camera_encode_request(void);
encoded_frame_t *camera_encode_wait(void);
void camera_encode_free( void );

/*----------------------------------------------------------------------------*/
#ifdef __cplusplus
}
#endif
#endif /* _CAMERA_APP_H_ */
