/**
 ******************************************************************************
 * @file    w6x_rtp_test.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements ST67 RTP sender test suite.
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 ******************************************************************************
 */
#include "w6x_rtp_sender.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Component
* @{
* @addtogroup W6X_RTP_Test
* @{
* @defgroup PUBLIC_Definitions          PUBLIC constants
* @defgroup PUBLIC_Macros               PUBLIC macros
* @defgroup PUBLIC_Types                PUBLIC data-types
* @defgroup PUBLIC_Data                 PUBLIC data / variables
* @defgroup PUBLIC_API                  PUBLIC API
* @defgroup PRIVATE_TUNABLES            PRIVATE compile-time tunables
* @defgroup PRIVATE_Definitions         PRIVATE constants
* @defgroup PRIVATE_Macros              PRIVATE macros
* @defgroup PRIVATE_Types               PRIVATE data-types
* @defgroup PRIVATE_Data                PRIVATE data / variables
* @defgroup PRIVATE_Functions           PRIVATE functions
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_TUNABLES
* @{
*//*--------------------------------------------------------------------------*/

/* Test mode selection */
#define TEST_MODE_BASIC         0U
#define TEST_MODE_USAGE         1U
#define TEST_MODE               TEST_MODE_USAGE

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

#define TEST_CNAME                  "ST67@ST.com"
#define TEST_PAYLOAD_TYPE           96
#define TEST_SESSION_SSRC           11478

#define TEST_RTP_DATA               "Test RTP packet data"
#define TEST_TIMESTAMP              123456U
#define TEST_NTP_MSW                123U
#define TEST_NTP_LSW                456U

#define TEST_CLIENT_SSRC            1052681868
#define TEST_CLIENT_CNAME           "cn-test-cname"
#define TEST_CLIENT_FRACLOSS        255
#define TEST_CLIENT_PKTLOSS         -1
#define TEST_CLIENT_EXTMAX          94974
#define TEST_CLIENT_JITTER          444
#define TEST_CLIENT_LAST_SR         0
#define TEST_CLIENT_DELAY           0

#define TEST_PAYLOAD_MAX_SIZE       80U

/* Test events */
#define TEST_EVT_HOST_IP            (1U << 0U)

#define TEST_EVT_RTCP_RR_OK         (1U << 1U)
#define TEST_EVT_RTCP_SDES_OK       (1U << 2U)
#define TEST_EVT_RTCP_OK            (TEST_EVT_RTCP_RR_OK | TEST_EVT_RTCP_SDES_OK)

#define TEST_EVT_RTCP_SR_OK         (1U << 3U)
#define TEST_EVT_RTP_RAW_OK         (1U << 4U)
#define TEST_EVT_RTP_OK             (TEST_EVT_RTCP_SR_OK | TEST_EVT_RTP_RAW_OK)

#define TEST_EVT_RTP_H264_SMALL     (1U << 5U)
#define TEST_EVT_RTP_H264_MEDIUM    (1U << 6U)
#define TEST_EVT_RTP_H264_LARGE     (1U << 7U)
#define TEST_EVT_RTP_H264_SEGMENTED (1U << 8U)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

#define TEST_ASSERT(_condition)             \
do                                          \
{                                           \
  if (!(_condition))                        \
  {                                         \
    RTP_LERROR("Test assertion failed!\n"); \
    _test_rtp_teardown();                   \
    return -1;                              \
  }                                         \
} while (0)

#define TEST_EQUAL(_expected, _actual) \
  TEST_ASSERT((_expected) == (_actual))

#define TEST_NOT_NULL(_ptr) \
  TEST_ASSERT((_ptr) != NULL)

#define TEST_ARRAY_EQUAL(_expected, _actual, _size) \
  TEST_ASSERT(0 == memcmp((_expected), (_actual), (_size)))

#define TEST_EVENT_WITH_TIMEOUT(_event, _timeout) \
  TEST_ASSERT((_event) & xEventGroupWaitBits(_rtp->event, (_event), pdFALSE, pdTRUE, pdMS_TO_TICKS(_timeout)))

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** RTP test function type */
typedef int32_t (*rtp_test_fn_t)(void);

/** RTP test object */
typedef struct
{
  w6x_rtp_sender_t    sender;
  w6x_rtp_session_t   session;
  EventGroupHandle_t  event;
  in_addr_t           host;
  uint32_t            wait;
  uint32_t            sequence;
} rtp_test_t;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

#if TEST_MODE == TEST_MODE_BASIC
/* Basic tests */
static int32_t _test_rtp_api(void);
static int32_t _test_rtp_lifecycle(void);

#elif TEST_MODE == TEST_MODE_USAGE
/* Usage tests */
static int32_t _test_rtp_setup(void);
static int32_t _test_rtp_basic(void);
static int32_t _test_rtp_h264(void);
static int32_t _test_rtp_teardown(void);

#endif /* TEST_MODE */

/* Callback handlers */
static int32_t _test_rtcp_raw_cb(w6x_rtp_sender_t *sender, sockaddr_in_t *client, uint8_t *packet, size_t size);
static int32_t _test_rtcp_rr_cb(w6x_rtp_session_t *session, w6x_rtcp_rr_t *report);
static int32_t _test_rtcp_sdes_cb(w6x_rtcp_sdes_t *info);

static int32_t _test_check_rtp(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size);
static int32_t _test_check_rtcp_sr(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size);

static int32_t _test_check_rtp_h264(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size);
static int32_t _test_check_rtp_h264_complete(size_t count, uint8_t *packet, size_t size);
static int32_t _test_check_rtp_h264_fragment(size_t count, uint8_t *packet, size_t size);
static int32_t _test_check_rtp_h264_segment(size_t count, uint8_t *packet, size_t size);

/* Utility functions */
static int32_t _test_rtcp_send(w6x_rtp_session_t *session, const uint8_t *data, size_t size);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static rtp_test_fn_t  _test_item[] = {
  #if TEST_MODE == TEST_MODE_BASIC
  _test_rtp_api,
  _test_rtp_lifecycle,
  #elif TEST_MODE == TEST_MODE_USAGE
  _test_rtp_setup,
  _test_rtp_basic,
  _test_rtp_h264,
  _test_rtp_teardown,
  #else
  #error "Invalid test selection"
  #endif /* TEST_MODE */
  /* End marker */
  NULL
};

#if TEST_MODE == TEST_MODE_USAGE
static rtp_test_t     *_rtp = NULL;

static uint8_t        _h264_data_small[] = {
  /* h264 header started with 0x000001 */
  0x00, 0x00, 0x01, 0x65,
  /* Test data */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
};

static uint8_t        _h264_data_medium[] = {
  /* h264 header started with 0x000001 */
  0x00, 0x00, 0x01, 0x65,
  /* Test data */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

static uint8_t        _h264_data_large[] = {
  /* h264 header started with 0x000001 */
  0x00, 0x00, 0x01, 0x65,
  /* Test data */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

static uint8_t        _h264_data_segmented[] = {
  /* h264 header started with 0x000001 */
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
  0x00, 0x00, 0x00, 0x01, 0x61, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
};

#endif /* TEST_MODE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t w6x_rtp_test(void)
{
  size_t  idx = 0;

  RTP_LINFO("\nRunning W6X RTP Test Suite...\n");
  while (_test_item[idx] != NULL)
  {
    if (_test_item[idx]() != RTP_OK)
    {
      RTP_LERROR("> Test %u failed!\n", idx);
      return -1;
    }
    idx++;
  }
  RTP_LINFO("> All tests succeeded!\n");
  return RTP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

#if TEST_MODE == TEST_MODE_BASIC
/* Basic tests */
static int32_t _test_rtp_api(void)
{
  in_addr_t         ip      = { .s_addr = 0xC0A8000FU };     /* 192.168.0.15 */
  w6x_rtp_sender_t  sender  = { 0 };
  w6x_rtp_session_t session = { 0 };
  uint16_t          rtp_port;
  uint16_t          rtcp_port;
  uint32_t          vu32;
  uint16_t          vu16;

  RTP_LINFO("> Validating API...\n");

  /* Sender Create ----------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_create(&sender, NULL, RTP_PORT));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_create(NULL, (uint8_t*)"test", RTP_PORT));

  /* ERROR: Params: Already initialized */
  sender.id = RTP_SENDER_ID;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_create(&sender, (uint8_t*)"test", RTP_PORT));

  /* ERROR: Params: Port 0 or odd */
  sender.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_create(&sender, (uint8_t*)"test", 0));
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_create(&sender, (uint8_t*)"test", RTP_PORT + 1U));

  /* Sender Get Ports -------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_get_port(&sender, NULL, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_get_port(&sender, &rtp_port, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_get_port(&sender, NULL, &rtcp_port));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_get_port(NULL, &rtp_port, &rtcp_port));

  /* ERROR: Params: Sender not ready */
  sender.id = 0;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_get_port(&sender, &rtp_port, &rtcp_port));

  /* Sender Set RTCP RR Callback --------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_set_rtcp_rr_callback(&sender, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_set_rtcp_rr_callback(NULL, _test_rtcp_rr_cb));

  /* ERROR: Params: Sender not ready */
  sender.id = 0;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_set_rtcp_rr_callback(&sender, _test_rtcp_rr_cb));

  /* Sender Set RTCP SDES Callback ------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_set_rtcp_sdes_callback(&sender, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_set_rtcp_sdes_callback(NULL, _test_rtcp_sdes_cb));

  /* ERROR: Params: Sender not ready */
  sender.id = 0;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_set_rtcp_sdes_callback(&sender, _test_rtcp_sdes_cb));

  /* Sender Delete ----------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_delete(NULL));

  /* ERROR: Params: Sender not ready */
  sender.id = 0;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_delete(&sender));

  /* Session Create ---------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_create(&sender, NULL, 0, RTP_PORT, RTP_PORT + 1U, ip));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_create(NULL, &session, 0, RTP_PORT, RTP_PORT + 1U, ip));

  /* ERROR: Params: Sender not initialized */
  sender.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 0, RTP_PORT, RTP_PORT + 1U, ip));

  /* ERROR: Params: Session already initialized */
  sender.id = RTP_SENDER_ID;
  session.id = RTP_SESSION_ID;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 0, RTP_PORT, RTP_PORT + 1U, ip));

  /* ERROR: Params: Payload type invalid */
  session.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 255, RTP_PORT, RTP_PORT + 1U, ip));

  /* ERROR: Params: Port invalid */
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 0, 0, RTP_PORT + 1U, ip));
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 0, RTP_PORT, 0, ip));

  /* ERROR: Params: IP invalid */
  ip.s_addr = IPADDR_ANY;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_create(&sender, &session, 0, RTP_PORT, RTP_PORT + 1U, ip));

  /* Session Get SSRC -------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_get_ssrc(&session, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_get_ssrc(NULL, &vu32));

  /* ERROR: Params: Session not initialized */
  session.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_get_ssrc(&session, &vu32));

  /* Session Get Sequence Number --------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_get_sequence_number(&session, NULL));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_get_sequence_number(NULL, &vu16));

  /* ERROR: Params: Session not initialized */
  session.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_get_sequence_number(&session, &vu16));

  /* Session Set Sample Factor ----------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_set_sample_factor(NULL, 0));

  /* ERROR: Params: Session not initialized */
  session.id = 0U;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_set_sample_factor(&session, 0));

  /* Session Send Packet ----------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_send_packet(&session, NULL, 1, 123456, 456, 123, 1));
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_send_packet(NULL, (uint8_t*)&vu32, 1, 123456, 456, 123, 1));

  /* ERROR: Params: Session not initialized */
  session.id    = 0U;
  session.sender= &sender;
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_session_send_packet(&session, (uint8_t*)&vu32, 1, 123456, 456, 123, 1));

  /* Session Delete ---------------------------*/
  /* ERROR: PTR NULL */
  TEST_EQUAL(RTP_ERROR_PTR, w6x_rtp_sender_session_delete(NULL));

  /* ALL OK -----------------------------------*/
  return RTP_OK;
}

static int32_t _test_rtp_lifecycle(void)
{
  w6x_rtp_sender_t  sender      = { 0 };
  w6x_rtp_session_t session[3U] = { 0 };
  uint16_t          rtp_port;
  uint16_t          rtcp_port;

  RTP_LINFO("> Validating lifecycle...\n");

  /* Sender Create ----------------------------*/
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_create(&sender, (uint8_t*)TEST_CNAME, RTP_PORT));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_set_rtcp_rr_callback(&sender, _test_rtcp_rr_cb));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_set_rtcp_sdes_callback(&sender, _test_rtcp_sdes_cb));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_get_port(&sender, &rtp_port, &rtcp_port));
  TEST_EQUAL(RTP_PORT, rtp_port);
  TEST_EQUAL(RTP_PORT + 1U, rtcp_port);

  /* Session Create ---------------------------*/
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_create(&sender, &session[0], TEST_PAYLOAD_TYPE, RTP_PORT + 2U, RTP_PORT + 3U, (in_addr_t){.s_addr = 0xC0A8000AU}));
  TEST_EQUAL(sender.session, &session[0]);
  TEST_EQUAL(session[0].next, NULL);

  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_create(&sender, &session[1], TEST_PAYLOAD_TYPE, RTP_PORT + 4U, RTP_PORT + 5U, (in_addr_t){.s_addr = 0xC0A8000BU}));
  TEST_EQUAL(session[0].next, &session[1]);
  TEST_EQUAL(session[1].next, NULL);

  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_create(&sender, &session[2], TEST_PAYLOAD_TYPE, RTP_PORT + 6U, RTP_PORT + 7U, (in_addr_t){.s_addr = 0xC0A8000CU}));
  TEST_EQUAL(session[1].next, &session[2]);
  TEST_EQUAL(session[2].next, NULL);

  /* Sender Delete Fail -----------------------*/
  TEST_EQUAL(RTP_ERROR_PARAMETERS, w6x_rtp_sender_delete(&sender));

  /* Session Delete ---------------------------*/
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_delete(&session[1]));
  TEST_EQUAL(session[0].next, &session[2]);

  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_delete(&session[0]));
  TEST_EQUAL(sender.session, &session[2]);

  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_delete(&session[2]));
  TEST_EQUAL(sender.session, NULL);

  /* Sender Delete ----------------------------*/
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_delete(&sender));

  /* ALL OK -----------------------------------*/
  return RTP_OK;
}

#elif TEST_MODE == TEST_MODE_USAGE
/* Usage tests */
static int32_t _test_rtp_setup(void)
{
  RTP_LINFO("> Setting up...\n");

  /* Prepare */
  _rtp = (rtp_test_t *)malloc(sizeof(rtp_test_t));
  TEST_NOT_NULL(_rtp);
  memset(_rtp, 0, sizeof(rtp_test_t));

  /* Create event group */
  _rtp->event = xEventGroupCreate();
  TEST_NOT_NULL(_rtp->event);

  /* Create sender */
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_create(&_rtp->sender, (uint8_t*)TEST_CNAME, RTP_PORT));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_set_rtcp_rr_callback(&_rtp->sender, _test_rtcp_rr_cb));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_set_rtcp_sdes_callback(&_rtp->sender, _test_rtcp_sdes_cb));
  _rtp->sender.rtcp_raw_cb = _test_rtcp_raw_cb;

  /* Wait for host */
  _rtp->wait = TEST_EVT_HOST_IP;
  RTP_LINFO("> Start test script! Waiting for host IP...\n");
  xEventGroupWaitBits(_rtp->event, _rtp->wait, pdFALSE, pdTRUE, portMAX_DELAY);

  /* Create sender */
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_create(&_rtp->sender, &_rtp->session, TEST_PAYLOAD_TYPE, RTP_PORT, RTP_PORT + 1U, _rtp->host));
  _rtp->session.ssrc = TEST_SESSION_SSRC;

  return RTP_OK;
}

static int32_t _test_rtp_basic(void)
{
  RTP_LINFO("> Testing basic operation...\n");

  /* TEST: RTCP RR */
  _rtp->wait = TEST_EVT_RTCP_OK;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"BASIC_RTCP", 10U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 1000U);

  /* TEST: RTP + RTCP SR */
  _rtp->wait = TEST_EVT_RTP_OK;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"BASIC_RTP", 9U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_send_packet(
    &_rtp->session, (uint8_t*)TEST_RTP_DATA, strlen(TEST_RTP_DATA),
    TEST_TIMESTAMP, TEST_NTP_MSW, TEST_NTP_LSW, 1U
  ));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 1000U);

  /* TEST: READY */
  return RTP_OK;
}

static int32_t _test_rtp_h264(void)
{
  size_t max_size;

  RTP_LINFO("> Testing H264 streaming...\n");

  /* Prepare server */
  max_size                          = _rtp->sender.rtp_payload_max_size;
  _rtp->sender.rtp_payload_max_size = TEST_PAYLOAD_MAX_SIZE;
  _rtp->sequence                    = _rtp->session.sequence_number;

  /* TEST: H264 small */
  _rtp->wait = TEST_EVT_RTP_H264_SMALL;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"H264_SMALL", 10U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_send_h264(
    &_rtp->session, _h264_data_small, sizeof(_h264_data_small),
    TEST_TIMESTAMP, TEST_NTP_MSW, TEST_NTP_LSW, 1U
  ));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 10000U);

  /* TEST: H264 medium */
  _rtp->wait = TEST_EVT_RTP_H264_MEDIUM;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"H264_MEDIUM", 11U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_send_h264(
    &_rtp->session, _h264_data_medium, sizeof(_h264_data_medium),
    TEST_TIMESTAMP, TEST_NTP_MSW, TEST_NTP_LSW, 1U
  ));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 10000U);

  /* TEST: H264 large */
  _rtp->wait = TEST_EVT_RTP_H264_LARGE;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"H264_LARGE", 10U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_send_h264(
    &_rtp->session, _h264_data_large, sizeof(_h264_data_large),
    TEST_TIMESTAMP, TEST_NTP_MSW, TEST_NTP_LSW, 1U
  ));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 10000U);

  /* TEST: H264 segmented */
  _rtp->wait = TEST_EVT_RTP_H264_SEGMENTED;
  TEST_EQUAL(RTP_OK, _test_rtcp_send(&_rtp->session, (uint8_t*)"H264_SEGMENTED", 14U));
  vTaskDelay(pdMS_TO_TICKS(10U));
  TEST_EQUAL(RTP_OK, w6x_rtp_sender_session_send_h264(
    &_rtp->session, _h264_data_segmented, sizeof(_h264_data_segmented),
    TEST_TIMESTAMP, TEST_NTP_MSW, TEST_NTP_LSW, 1U
  ));
  TEST_EVENT_WITH_TIMEOUT(_rtp->wait, 10000U);

  /* TEST: READY */
  _rtp->sender.rtp_payload_max_size = max_size;
  return RTP_OK;
}

static int32_t _test_rtp_teardown(void)
{
  RTP_LINFO("> Tearing down...\n");

  /* Cleanup */
  if (_rtp != NULL)
  {
    /* Send STOP command */
    _test_rtcp_send(&_rtp->session, (uint8_t*)"STOP", 4U);
    vTaskDelay(pdMS_TO_TICKS(1000U));

    /* Free resources */
    w6x_rtp_sender_session_delete(&_rtp->session);
    w6x_rtp_sender_delete(&_rtp->sender);
    vEventGroupDelete(_rtp->event);
    free(_rtp);
  }
  return RTP_OK;
}

#endif /* TEST_MODE */

/* Callback handlers */
static int32_t _test_rtcp_raw_cb(w6x_rtp_sender_t *sender, sockaddr_in_t *client, uint8_t *packet, size_t size)
{
  switch (_rtp->wait)
  {
    case TEST_EVT_HOST_IP:
      if (strncmp((char*)packet, "START", 5U) == 0)
      {
        _rtp->host = client->sin_addr.s_addr;
        xEventGroupSetBits(_rtp->event, TEST_EVT_HOST_IP);
      }
      break;

    case TEST_EVT_RTP_OK:
      if (strncmp((char*)packet, "RTP", 3U) == 0)
      {
        _test_check_rtp(sender, (uint8_t*)(packet + 3U), (size - 3U));
      }
      else if (strncmp((char*)packet, "RTCP", 4U) == 0)
      {
        _test_check_rtcp_sr(sender, (uint8_t*)(packet + 4U), (size - 4U));
      }
      break;

    case TEST_EVT_RTP_H264_SMALL:
    case TEST_EVT_RTP_H264_MEDIUM:
    case TEST_EVT_RTP_H264_LARGE:
    case TEST_EVT_RTP_H264_SEGMENTED:
      _test_check_rtp_h264(sender, (uint8_t*)packet, size);
      break;

    default:
      /* Unknown state */
      break;
  }
  return RTP_OK;
}

static int32_t _test_rtcp_rr_cb(w6x_rtp_session_t *session, w6x_rtcp_rr_t *report)
{
  /* Validate */
  if (_rtp->wait == TEST_EVT_RTCP_OK)
  {
    /* Run tests */
    TEST_EQUAL(report->client, TEST_CLIENT_SSRC);
    TEST_EQUAL(report->fraction_loss, TEST_CLIENT_FRACLOSS);
    TEST_EQUAL(report->packet_loss, TEST_CLIENT_PKTLOSS);
    TEST_EQUAL(report->extended_max, TEST_CLIENT_EXTMAX);
    TEST_EQUAL(report->jitter, TEST_CLIENT_JITTER);
    TEST_EQUAL(report->last_sr, TEST_CLIENT_LAST_SR);
    TEST_EQUAL(report->delay, TEST_CLIENT_DELAY);

    /* Report test ready */
    xEventGroupSetBits(_rtp->event, TEST_EVT_RTCP_RR_OK);
  }
  return RTP_OK;
}

static int32_t _test_rtcp_sdes_cb(w6x_rtcp_sdes_t *info)
{
  /* Validate */
  if (_rtp->wait == TEST_EVT_RTCP_OK)
  {
    uint8_t cname[]= TEST_CLIENT_CNAME;
    size_t  size   = strlen((char*)cname);

    /* Perform SDES tests */
    TEST_EQUAL(info->client, TEST_CLIENT_SSRC);
    TEST_EQUAL(info->cname_size, size);
    TEST_ARRAY_EQUAL(cname, info->cname, size);

    /* Report test ready */
    xEventGroupSetBits(_rtp->event, TEST_EVT_RTCP_SDES_OK);
  }
  return RTP_OK;
}

static int32_t _test_check_rtp(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size)
{
  /* Perform RTP tests */
  w6x_rtp_pkt_header_t *header = (w6x_rtp_pkt_header_t*)packet;
  U16_ENDIAN_SWAP_IP(header->sequence_number);
  U32_ENDIAN_SWAP_IP(header->rtp_timestamp);
  U32_ENDIAN_SWAP_IP(header->session);

  TEST_EQUAL(RTP_HEADER_VERSION, header->byte0);
  TEST_EQUAL(RTP_HEADER_MARKER | TEST_PAYLOAD_TYPE, header->byte1);
  TEST_EQUAL(sender->session->sequence_number - 1U, header->sequence_number);
  TEST_EQUAL(sender->session->rtp_timestamp, header->rtp_timestamp);
  TEST_EQUAL(sender->session->ssrc, header->session);

  uint8_t data[]    = TEST_RTP_DATA;
  uint8_t data_size = strlen((char*)data);
  uint8_t *payload  = ((uint8_t*)header + sizeof(w6x_rtp_pkt_header_t));
  TEST_ARRAY_EQUAL(data, payload, data_size);

  /* Report test ready */
  xEventGroupSetBits(_rtp->event, TEST_EVT_RTP_OK);
  return RTP_OK;
}

static int32_t _test_check_rtcp_sr(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size)
{
  /* Perform SR tests */
  w6x_rtcp_pkt_header_t *sr_header = (w6x_rtcp_pkt_header_t*)packet;
  U16_ENDIAN_SWAP_IP(sr_header->size);

  TEST_EQUAL(RTP_HEADER_VERSION, sr_header->byte0);
  TEST_EQUAL(RTCP_HEADER_TYPE_SR, sr_header->packet_type);
  TEST_EQUAL((4U * sr_header->size), U32_ALIGN(sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sr_t)));

  w6x_rtcp_pkt_sr_t *sr_payload = (w6x_rtcp_pkt_sr_t*)((uint8_t*)sr_header + sizeof(w6x_rtcp_pkt_header_t));
  U32_ENDIAN_SWAP_IP(sr_payload->session);
  U32_ENDIAN_SWAP_IP(sr_payload->ntp_msw);
  U32_ENDIAN_SWAP_IP(sr_payload->ntp_lsw);
  U32_ENDIAN_SWAP_IP(sr_payload->rtp_timestamp);
  U32_ENDIAN_SWAP_IP(sr_payload->packet_count);
  U32_ENDIAN_SWAP_IP(sr_payload->octet_count);

  TEST_EQUAL(sender->session->ssrc, sr_payload->session);
  TEST_EQUAL(sender->session->rtp_timestamp, sr_payload->rtp_timestamp);
  TEST_EQUAL(sender->session->ntp_msw, sr_payload->ntp_msw);
  TEST_EQUAL(sender->session->ntp_lsw, sr_payload->ntp_lsw);
  TEST_EQUAL(sender->session->packet_count - 1U, sr_payload->packet_count);
  TEST_EQUAL(0, sr_payload->octet_count);

  /* Perform SDES tests */
  w6x_rtcp_pkt_header_t *sdes_header = (w6x_rtcp_pkt_header_t*)((uint8_t*)sr_payload + sizeof(w6x_rtcp_pkt_sr_t));
  U16_ENDIAN_SWAP_IP(sdes_header->size);

  TEST_EQUAL(RTP_HEADER_VERSION | 1U, sdes_header->byte0);
  TEST_EQUAL(RTCP_HEADER_TYPE_SDES, sdes_header->packet_type);
  TEST_EQUAL((4U * sdes_header->size), U32_ALIGN(sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sdes_chunk_t) + sender->cname_size - 1U));

  w6x_rtcp_pkt_sdes_chunk_t *sdes_chunk = (w6x_rtcp_pkt_sdes_chunk_t*)((uint8_t*)sdes_header + sizeof(w6x_rtcp_pkt_header_t));
  U32_ENDIAN_SWAP_IP(sdes_chunk->source);

  TEST_EQUAL(sender->session->ssrc, sdes_chunk->source);
  TEST_EQUAL(RTCP_SDES_TYPE_CNAME, sdes_chunk->item[0].type);
  TEST_ARRAY_EQUAL(sender->cname, sdes_chunk->item[0].data, sender->cname_size);

  /* Report test ready */
  xEventGroupSetBits(_rtp->event, TEST_EVT_RTCP_SR_OK);
  return RTP_OK;
}


static int32_t _test_check_rtp_h264(w6x_rtp_sender_t *sender, uint8_t *packet, size_t size)
{
  static size_t count = 0U;

  /* Perform h264 tests */
  w6x_rtp_pkt_header_t *header = (w6x_rtp_pkt_header_t*)packet;
  U16_ENDIAN_SWAP_IP(header->sequence_number);
  U32_ENDIAN_SWAP_IP(header->rtp_timestamp);
  U32_ENDIAN_SWAP_IP(header->session);

  TEST_EQUAL(RTP_HEADER_VERSION, header->byte0);
  TEST_EQUAL(_rtp->sequence + count, header->sequence_number);
  TEST_EQUAL(sender->session->rtp_timestamp, header->rtp_timestamp);
  TEST_EQUAL(sender->session->ssrc, header->session);
  if ((count == 0U) | (count == 2U) | (count == 5U) | (count == 13U))
  {
    TEST_EQUAL(RTP_HEADER_MARKER | TEST_PAYLOAD_TYPE, header->byte1);
  }
  else
  {
    TEST_EQUAL(TEST_PAYLOAD_TYPE, header->byte1);
  }

  /* Validate payloads */
  uint8_t *payload    = packet + sizeof(w6x_rtp_pkt_header_t);
  size_t  payload_size= size - sizeof(w6x_rtp_pkt_header_t);
  if (count >= 6U)      /* Segmented */
  {
    _test_check_rtp_h264_segment(count, payload, payload_size);
  }
  else if (count >= 1U) /* Medium/Large */
  {
    _test_check_rtp_h264_fragment(count, payload, payload_size);
  }
  else                  /* Small */
  {
    _test_check_rtp_h264_complete(count, payload, payload_size);
  }

  /* Go to next item */
  count++;
  if ((count == 1U) || (count == 3U) || (count == 6U) || (count == 14U))
  {
    xEventGroupSetBits(_rtp->event, _rtp->wait);
  }
  return RTP_OK;
}

static int32_t _test_check_rtp_h264_complete(size_t count, uint8_t *packet, size_t size)
{
  /* Single NAL: first bit is the same as frame. Also check expected size and compare data */
  TEST_EQUAL(11U, size);
  TEST_EQUAL(_h264_data_small[3U], packet[0U]);
  TEST_ARRAY_EQUAL(&_h264_data_small[4U], packet + 1U, size - 1U);
  return RTP_OK;
}

static int32_t _test_check_rtp_h264_fragment(size_t count, uint8_t *packet, size_t size)
{
  uint8_t *check;

  /* Validate FU-A mode */
  TEST_EQUAL(0x7CU, packet[0U]);
  switch (count)
  {
    case 1:
      TEST_EQUAL(0x85U, packet[1U]);   /* FU start */
      check = _h264_data_medium + 4U;
      break;

    case 2:
      TEST_EQUAL(0x45U, packet[1U]);   /* FU end */
      check = _h264_data_medium + (TEST_PAYLOAD_MAX_SIZE - 2U) + 3U;
      break;

    case 3:
      TEST_EQUAL(0x85U, packet[1U]);   /* FU start */
      check = _h264_data_large + 4U;
      break;

    case 4:
      TEST_EQUAL(0x05U, packet[1U]);   /* FU item */
      check = _h264_data_large + (TEST_PAYLOAD_MAX_SIZE - 2U) + 3U;
      break;

    case 5:
      TEST_EQUAL(0x45U, packet[1U]);   /* FU end */
      check = _h264_data_large + (2U * (TEST_PAYLOAD_MAX_SIZE - 2U)) + 3U;
      break;

    default:
      TEST_ASSERT(0);
  }

  /* Validate payload */
  TEST_ARRAY_EQUAL(check, packet + 2U, size - 2U);
  return RTP_OK;
}

static int32_t _test_check_rtp_h264_segment(size_t count, uint8_t *packet, size_t size)
{
  /* Single NAL: first bit is the same as frame. Also check expected size and compare data */
  TEST_EQUAL(12U, size);
  TEST_EQUAL(_h264_data_segmented[4U], packet[0U]);
  TEST_ARRAY_EQUAL(&_h264_data_segmented[5U], packet + 1U, size - 1U);
  return RTP_OK;
}

/* Utility functions */
static int32_t _test_rtcp_send(w6x_rtp_session_t *session, const uint8_t *data, size_t size)
{
  /* Validate */
  if ((session == NULL) || (session->sender == NULL) || (data == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if ((session->sender->id != RTP_SENDER_ID) || (session->id != RTP_SESSION_ID))
  {
    return RTP_ERROR_PARAMETERS;
  }
  if (size == 0)
  {
    return RTP_OK;
  }

  /* Send RTP packet */
  int32_t status = W6X_Net_Sendto(
    session->sender->rtcp_socket, data, size, 0,
    (const sockaddr_t*)&session->client_rtcp, sizeof(session->client_rtcp)
  );
  return (status < 0) ? RTP_ERROR_SOCKET : RTP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Component -->
* @} <!-- End: W6X_RTP_Test -->
*//*--------------------------------------------------------------------------*/
