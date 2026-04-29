/**
 ******************************************************************************
 * @file    w6x_rtp_sender.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements ST67 RTP sender.
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
* @addtogroup W6X_RTP_Sender
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

/* RTP test configuration */
#define RTP_PORT_BIND             0U
#define RTCP_PORT_BIND            1U

/* Timeouts */
#define RTP_RX_TIMEOUT            1000U
#define RTP_TX_TIMEOUT            100U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* RTCP task
 * TODO: Optimize priority and stack size
 */
#define RTCP_TASK_PRIO            ((configMAX_PRIORITIES / 2U) - 1U)
#define RTCP_TASK_STACK_SIZE      (3U * 1024U)

#define RTCP_TASK_EVT_ABORT       BIT(0U)
#define RTCP_TASK_EVT_START       BIT(1U)
#define RTCP_TASK_EVT_COMPLETED   BIT(2U)

/* Packet definitions */
#define RTP_PAYLOAD_MAX_SIZE      (UDP_PAYLOAD_MAX_SIZE - sizeof(w6x_rtp_pkt_header_t))

#define RTCP_PACKET_SDES_SIZE     U32_ALIGN(sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sdes_chunk_t) + RTP_CNAME_MAX_SIZE - 1U)
#define RTCP_PACKET_SR_SIZE       U32_ALIGN(sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sr_t))
#define RTCP_PACKET_SIZE          (RTCP_PACKET_SDES_SIZE + RTCP_PACKET_SR_SIZE)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

#define PORT_CHECK(x)             ((x) != 0U)
#define PORT_CHECK_EVEN(x)        (PORT_CHECK(x) && (((x) % 2U) == 0U))

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

int32_t         w6x_socket_udp_create(
  bool              bind,
  uint16_t          port
);

static void     _w6x_rtp_sender_cleanup(
  w6x_rtp_sender_t  *sender
);

static int32_t  _w6x_rtp_sender_session_find(
  w6x_rtp_sender_t  *sender,
  w6x_rtp_session_t **session,
  uint32_t          ssrc
);

static void     _w6x_rtcp_packet_rx_task(
  void              *args
);

static int32_t  _w6x_rtcp_process_packet(
  w6x_rtp_sender_t  *sender,
  uint8_t           *packet,
  size_t            size
);

static int32_t  _w6x_rtcp_process_packet_rr(
  w6x_rtp_sender_t      *sender,
  w6x_rtcp_pkt_header_t *header
);

static int32_t  _w6x_rtcp_process_packet_sdes(
  w6x_rtp_sender_t      *sender,
  w6x_rtcp_pkt_header_t *header
);

static int32_t  _w6x_rtcp_send_packet(
  w6x_rtp_session_t *session
);

static int32_t  _w6x_rtcp_append_packet_sdes(
  w6x_rtp_session_t *session,
  uint8_t           **head,
  const uint8_t     *tail
);

static int32_t  _w6x_rtcp_append_packet_sr(
  w6x_rtp_session_t *session,
  uint8_t           **head,
  const uint8_t     *tail
);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t w6x_rtp_sender_create(
  w6x_rtp_sender_t  *sender,
  uint8_t           *cname,
  uint16_t          port
)
{
  uint8_t cname_size;
  int32_t status;

  /* Validate */
  if ((sender == NULL) || (cname == NULL))
  {
    return RTP_ERROR_PTR;
  }
  cname_size = (uint8_t)strlen((char*)cname);
  if (
    (sender->id == RTP_SENDER_ID) ||  /* sender already initialized */
    (cname_size == 0)             ||  /* cname empty */
    !PORT_CHECK_EVEN(port)            /* RTP valid port */
  )
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Reset sender */
  memset(sender, 0, sizeof(w6x_rtp_sender_t));
  sender->rtp_socket  = -1;
  sender->rtcp_socket = -1;

  /* Init RTOS support */
  sender->rtcp_event = xEventGroupCreate();
  if (sender->rtcp_event == NULL)
  {
    _w6x_rtp_sender_cleanup(sender);
    return RTP_ERROR_RTOS;
  }
  sender->mutex = xSemaphoreCreateMutex();
  if (sender->mutex == NULL)
  {
    _w6x_rtp_sender_cleanup(sender);
    return RTP_ERROR_RTOS;
  }
  snprintf((char*)sender->rtcp_task_name, RTP_CNAME_MAX_SIZE, "rtcp_task_%lu", (uint32_t)sender);
  status = xTaskCreate(
    _w6x_rtcp_packet_rx_task, (const char*)sender->rtcp_task_name,
    RTCP_TASK_STACK_SIZE / sizeof(StackType_t),
    (void*)sender, RTCP_TASK_PRIO, &sender->rtcp_task
  );
  if (status != pdPASS)
  {
    _w6x_rtp_sender_cleanup(sender);
    return RTP_ERROR_RTOS;
  }

  /* Create UDP sockets */
  sender->rtp_socket = w6x_socket_udp_create(RTP_PORT_BIND, port);
  if (sender->rtp_socket < 0)
  {
    _w6x_rtp_sender_cleanup(sender);
    return RTP_ERROR_SOCKET;
  }
  sender->rtcp_socket = w6x_socket_udp_create(RTCP_PORT_BIND, port + 1U);
  if (sender->rtcp_socket < 0)
  {
    _w6x_rtp_sender_cleanup(sender);
    return RTP_ERROR_SOCKET;
  }

  /* Update variables */
  sender->id                  = RTP_SENDER_ID;
  sender->port                = htons(port);
  sender->rtp_payload_max_size= RTP_PAYLOAD_MAX_SIZE;
  sender->cname_size          = (uint8_t)cname_size;
  memcpy(sender->cname, cname, cname_size);
  xEventGroupSetBits(sender->rtcp_event, RTCP_TASK_EVT_START);
  return RTP_OK;
}

int32_t w6x_rtp_sender_get_port(
  w6x_rtp_sender_t  *sender,
  uint16_t          *rtp_port,
  uint16_t          *rtcp_port
)
{
  /* Validate */
  if ((sender == NULL) || (rtp_port == NULL) || (rtcp_port == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Get ports */
  *rtp_port   = PP_NTOHS(sender->port);
  *rtcp_port  = *rtp_port + 1U;
  return RTP_OK;
}

int32_t w6x_rtp_sender_set_rtcp_rr_callback(
  w6x_rtp_sender_t  *sender,
  w6x_rtcp_rr_cb    callback
)
{
  /* Validate */
  if ((sender == NULL) || (callback == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Set callback */
  sender->rtcp_rr_cb = callback;
  return RTP_OK;
}

int32_t w6x_rtp_sender_set_rtcp_sdes_callback(
  w6x_rtp_sender_t  *sender,
  w6x_rtcp_sdes_cb  callback
)
{
  /* Validate */
  if ((sender == NULL) || (callback == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Set callback */
  sender->rtcp_sdes_cb = callback;
  return RTP_OK;
}

int32_t w6x_rtp_sender_delete(
  w6x_rtp_sender_t  *sender
)
{
  /* Validate */
  if (sender == NULL)
  {
    return RTP_ERROR_PTR;
  }
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Acquire mutex */
  xSemaphoreTake(sender->mutex, portMAX_DELAY);

  /* Only do this if all sessions are closed */
  if (sender->session != NULL)
  {
    /* Release and return */
    xSemaphoreGive(sender->mutex);
    return RTP_ERROR_PARAMETERS;
  }

  /* Reset sender ID */
  sender->id = 0;

  /* Release mutex */
  xSemaphoreGive(sender->mutex);

  /* Cleanup sender */
  _w6x_rtp_sender_cleanup(sender);
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_create(
  w6x_rtp_sender_t    *sender,
  w6x_rtp_session_t   *session,
  uint16_t            payload_type,
  uint16_t            rtp_port,
  uint16_t            rtcp_port,
  in_addr_t           ip
)
{
  /* Validate */
  if ((sender == NULL) || (session == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (
    (sender->id != RTP_SENDER_ID)         ||  /* Sender not initialized */
    (session->id == RTP_SESSION_ID)       ||  /* Session already initialized */
    (payload_type > RTP_PAYLOAD_MAX_TYPE) ||  /* Invalid payload type */
    (ip == IPADDR_ANY)                    ||  /* Invalid IP address */
    !(PORT_CHECK(rtp_port) && PORT_CHECK(rtcp_port))  /* RTP and RTCP valid ports */
  )
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Reset session */
  memset(session, 0, sizeof(w6x_rtp_session_t));

  /* Configure peer addresses */
  sockaddr_in_t *peer  = (sockaddr_in_t*)&session->client_rtp;
  peer->sin_family     = AF_INET;
  peer->sin_port       = htons(rtp_port);
  peer->sin_addr.s_addr= htonl(ip);

  peer = (sockaddr_in_t*)&session->client_rtcp;
  peer->sin_family     = AF_INET;
  peer->sin_port       = htons(rtcp_port);
  peer->sin_addr.s_addr= htonl(ip);

  /* Acquire mutex */
  xSemaphoreTake(sender->mutex, portMAX_DELAY);

  /* Link session to sender */
  if (sender->session != NULL)
  {
    w6x_rtp_session_t *tail = sender->session;
    while (tail->next)
    {
      tail = tail->next;
    }
    tail->next = session;
  }
  else
  {
    sender->session = session;
  }

  /* Release mutex */
  xSemaphoreGive(sender->mutex);

  /* Update variables */
  session->id             = RTP_SESSION_ID;
  session->payload_type   = payload_type;
  session->ssrc           = (uint32_t)rand();
  session->sequence_number= (uint16_t)rand();
  session->sender         = sender;
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_get_ssrc(
  w6x_rtp_session_t *session,
  uint32_t          *ssrc
)
{
  /* Validate */
  if ((session == NULL) || (ssrc == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (session->id != RTP_SESSION_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Get SSRC */
  *ssrc = session->ssrc;
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_get_sequence_number(
  w6x_rtp_session_t *session,
  uint16_t          *number
)
{
  /* Validate */
  if ((session == NULL) || (number == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (session->id != RTP_SESSION_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Get SSRC */
  *number = session->sequence_number;
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_set_sample_factor(
  w6x_rtp_session_t *session,
  uint32_t          factor
)
{
  /* Validate */
  if (session == NULL)
  {
    return RTP_ERROR_PTR;
  }
  if (session->id != RTP_SESSION_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Get SSRC */
  session->sample_factor = factor;
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_send_packet(
  w6x_rtp_session_t *session,
  uint8_t           *data,
  size_t            size,
  uint32_t          timestamp,
  uint32_t          ntp_msw,
  uint32_t          ntp_lsw,
  uint8_t           marker
)
{
  /* Validate */
  if ((session == NULL) || (session->sender == NULL) || (data == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (session->id != RTP_SESSION_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }
  if (size == 0U)
  {
    return RTP_OK;
  }

  /* Send packet */
  w6x_rtp_pkt_header_t  *header   = (w6x_rtp_pkt_header_t*)session->sender->rtp_packet;
  uint8_t               *payload  = session->sender->rtp_packet + sizeof(w6x_rtp_pkt_header_t);
  uint8_t               *head     = data;
  uint8_t               *tail     = data + size;
  size_t                payload_size;
  int32_t               status;

  /* Capture mutex and perform send */
  xSemaphoreTake(session->sender->mutex, portMAX_DELAY);
  while (head < tail)
  {
    /* Prepare payload */
    payload_size = (size_t)(((size_t)(tail - head) > session->sender->rtp_payload_max_size)? session->sender->rtp_payload_max_size : (tail - head));
    memcpy(payload, head, payload_size);
    head += payload_size;

    /* Prepare header
     * byte0: Version (2), Padding (0), Extension (0), CSRC count (0)
     *   - Only version is required (always 2)
     * byte1: Marker (1), Payload type (7)
     *  - Use payload type set in session
     *  - Marker is set if sample factor is set or if this is the last packet in the session
     */
    header->byte0 = RTP_HEADER_VERSION;
    header->byte1 = session->payload_type;
    if (session->sample_factor || (head == tail))
    {
      header->byte1 |= (uint8_t)(marker? RTP_HEADER_MARKER : 0U);
    }
    header->sequence_number = session->sequence_number;
    header->rtp_timestamp   = timestamp;
    header->session         = session->ssrc;

    /* Fix endianness for fields to work */
    U16_ENDIAN_SWAP_IP(header->sequence_number);
    U32_ENDIAN_SWAP_IP(header->rtp_timestamp);
    U32_ENDIAN_SWAP_IP(header->session);

    /* Store timestamps for RTCP report */
    session->rtp_timestamp  = timestamp;
    session->ntp_msw        = ntp_msw;
    session->ntp_lsw        = ntp_lsw;
    _w6x_rtcp_send_packet(session);

    /* Send RTP packet */
    status = socket_sendto(
      session->sender->rtp_socket, session->sender->rtp_packet, (size_t)(sizeof(w6x_rtp_pkt_header_t) + payload_size), 0,
      (const sockaddr_t*)&session->client_rtp, sizeof(session->client_rtp)
    );
    if (status < 0)
    {
      /* Release and return */
      xSemaphoreGive(session->sender->mutex);
      return RTP_ERROR_SOCKET;
    }

    /* Update SR statistics */
    session->octet_count += payload_size;
    session->sequence_number++;
    session->packet_count++;

    /* Update timestamp for sample-mode if more data is available */
    if (session->sample_factor && (head < tail))
    {
      timestamp += payload_size / session->sample_factor;
    }
  }
  /* Release and return */
  xSemaphoreGive(session->sender->mutex);
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_send_h264(
  w6x_rtp_session_t *session,
  uint8_t           *frame,
  size_t            size,
  uint32_t          timestamp,
  uint32_t          ntp_msw,
  uint32_t          ntp_lsw,
  uint8_t           marker
)
{
  /* Validate
   * NOTE: Marker should always be 1 since a full H264 frame is required!
   */
  if ((session == NULL) || (session->sender == NULL) || (frame == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if ((session->id != RTP_SESSION_ID) || (size <= 4U) || (marker != 1U))
  {
    return RTP_ERROR_PARAMETERS;
  }
  if (size == 0U)
  {
    return RTP_OK;
  }

  /* Prepare */
  uint8_t *head = frame;
  uint8_t *tail = frame + size;

  /* Skip H264 header */
  if ((head[0U] == 0x00U) && (head[1U] == 0x00U))
  {
    if ((head[2U] == 0x00U) && (head[3U] == 0x01U))
    {
      head += 4U;
    }
    else if (head[2U] == 0x01U)
    {
      head += 3U;
    }
  }
  if (head == frame)
  {
    /* Invalid header */
    return RTP_ERROR_PARAMETERS;
  }

  /* Process H264 data. This expects:
   * 1. Data frame is composed of several slices
   * 2. Special frames (SEI, SPS, PPS) are passed with a data frames
   */
  uint8_t *nal_frame;
  uint8_t nal_type;
  size_t  nal_size;
  int32_t status;
  while (head < (tail - 4U))
  {
    /* Reset frame */
    nal_frame = head;
    nal_size  = 0U;
    marker    = 0U;

    /* Extract frame using headers as reference) */
    while (1U)
    {
      /* Frame found if next header is found */
      if ((head[0U] == 0x00U) && (head[1U] == 0x00U))
      {
        if ((head[2U] == 0x00U) && (head[3U] == 0x01U))
        {
          nal_size = (size_t)(head - nal_frame);
          head     += 4U;
          break;
        }
        else if (head[2U] == 0x01U)
        {
          nal_size = (size_t)(head - nal_frame);
          head     += 3U;
          break;
        }
      }

      /* Frame found if last bytes are reached */
      if (head >= (tail - 4U))
      {
        /* No more data, add marker */
        nal_size = (size_t)(tail - nal_frame);
        marker   = 1U;
        head     = tail;
        break;
      }

      /* Move to next byte */
      head++;
    }

    /* Clear marker for special frames */
    nal_type = nal_frame[0U];
    if (
      ((nal_type & RTP_H264_TYPE_MASK) == RTP_H264_TYPE_SEI) ||
      ((nal_type & RTP_H264_TYPE_MASK) == RTP_H264_TYPE_SPS) ||
      ((nal_type & RTP_H264_TYPE_MASK) == RTP_H264_TYPE_PPS)
    )
    {
      marker = 0U;
    }

    /* Send frame depending on size */
    if (nal_size < session->sender->rtp_payload_max_size)
    {
      /* Single packet: Nothing to do. Same header is valid */
      status = w6x_rtp_sender_session_send_packet(session, nal_frame, nal_size, timestamp, ntp_msw, ntp_lsw, marker);
      if (status != RTP_OK)
      {
        return status;
      }
    }
    else
    {
      /* Multiple packet: Need to include FU indicator/header
       * NOTE: To reduce footprint this will be done in-place
       */
      uint8_t fu_marker;
      uint8_t fu_payload[RTP_PAYLOAD_MAX_SIZE];
      size_t  fu_size_max = session->sender->rtp_payload_max_size - RTP_H264_FU_SIZE;
      size_t  fu_size_last= (nal_size % fu_size_max);
      size_t  fu_packets  = ((nal_size - 1U) / fu_size_max) + 1U;
      size_t  fu_size;

      /* Prepare FU header */
      fu_payload[0U] = (uint8_t)((nal_type & RTP_H264_NRI_MASK) | RTP_H264_TYPE_FU_A);
      fu_payload[1U] = (uint8_t)(nal_type & RTP_H264_TYPE_MASK);

      /* Send packets */
      for (size_t idx = 0U; idx < fu_packets; idx++)
      {
        /* Prepare packet */
        if (idx == 0U)
        {
          /* First packet: Set start bit and ignore NAL type */
          fu_payload[1U] |= RTP_H264_FU_START_MASK;
          fu_size         = fu_size_max - 1U;
          fu_marker       = 0U;
          nal_frame++;
        }
        else if (idx == (fu_packets - 1U))
        {
          /* Last packet: Clear start bit, set end bit, and configure actual marker */
          fu_payload[1U] &= ~RTP_H264_FU_START_MASK;
          fu_payload[1U] |= RTP_H264_FU_END_MASK;
          fu_size         = fu_size_last? fu_size_last : fu_size_max;
          fu_marker       = marker;
        }
        else
        {
          /* Middle packet: Clear start bit */
          fu_payload[1U] &= ~RTP_H264_FU_START_MASK;
          fu_size         = fu_size_max;
        }

        /* Prepare and send packet */
        memcpy(&fu_payload[RTP_H264_FU_SIZE], nal_frame, fu_size);
        status = w6x_rtp_sender_session_send_packet(session, fu_payload, fu_size + RTP_H264_FU_SIZE, timestamp, ntp_msw, ntp_lsw, fu_marker);
        if (status != RTP_OK)
        {
          return status;
        }
        nal_frame += fu_size;
      }
    }
  }
  return RTP_OK;
}

int32_t w6x_rtp_sender_session_delete(
  w6x_rtp_session_t *session
)
{
  /* Validate */
  if ((session == NULL) || (session->sender == NULL))
  {
    return RTP_ERROR_PTR;
  }
  if (session->id != RTP_SESSION_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Get sender */
  w6x_rtp_sender_t *sender = session->sender;

  /* Reset session ID */
  session->id = 0;

  /* Acquire mutex */
  xSemaphoreTake(sender->mutex, portMAX_DELAY);

  /* Unlink session from sender */
  if (sender->session == session)
  {
    sender->session = session->next;
  }
  else
  {
    w6x_rtp_session_t *prev = sender->session;
    w6x_rtp_session_t *this = prev->next;
    while (this)
    {
      if (this == session)
      {
        prev->next = this->next;
        break;
      }
      prev = this;
      this = prev->next;
    }
  }

  /* Release mutex and return */
  xSemaphoreGive(sender->mutex);
  session->sender = NULL;
  return RTP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Create a UDP socket
 * @param bind 1 to bind the socket, 0 to create a client socket
 * @param port Port number to bind the socket to (if bind is 1)
 * @return Socket id on success, negative value on error
 */
int32_t         w6x_socket_udp_create(
  bool              bind,
  uint16_t          port
)
{
  timeval_t timeval;
  int32_t   instance;
  int32_t   status;

  /* Create socket */
  status = socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (status < 0)
  {
    return RTP_ERROR_SOCKET;
  }
  instance = status;

  /* Set IP ToS/DSCP to 4 (0x20) for real-time RTP */
  int tos = 0xB8; // DSCP 4
  status = socket_setopt(instance, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

  if (status != 0) {
      socket_close(instance);
      return RTP_ERROR_SOCKET;
  }

  /* Set timing */
  SET_TIMEVAL_IP(timeval, RTP_RX_TIMEOUT);
  status  = socket_setopt(instance, SOL_SOCKET, SO_RCVTIMEO, &timeval, sizeof(timeval_t));
  SET_TIMEVAL_IP(timeval, RTP_TX_TIMEOUT);
  status += socket_setopt(instance, SOL_SOCKET, SO_SNDTIMEO, &timeval, sizeof(timeval_t));
  if (status != 0)
  {
    socket_close(instance);
    return RTP_ERROR_SOCKET;
  }

  /* Bind to port */
  if (bind)
  {
    sockaddr_in_t address = {
      .sin_family     = AF_INET,
      .sin_port       = htons(port),
      .sin_addr.s_addr= htonl(IPADDR_ANY)
    };
    status = socket_bind(instance, (const sockaddr_t*)&address, sizeof(address));
    if (status != 0)
    {
      socket_close(instance);
      return RTP_ERROR_SOCKET;
    }
  }
  return instance;
}

/**
 * @brief Cleanup the RTP sender
 * @param sender Pointer to the RTP sender to clean up
 */
static void 	 	_w6x_rtp_sender_cleanup(
  w6x_rtp_sender_t  *sender
)
{
  if (sender->rtcp_task != NULL)
  {
    xEventGroupSetBits(sender->rtcp_event, RTCP_TASK_EVT_ABORT);
    xEventGroupWaitBits(sender->rtcp_event, RTCP_TASK_EVT_COMPLETED, pdFALSE, pdFALSE, portMAX_DELAY);
  }
  if (sender->rtp_socket >= 0)
  {
    #if RTP_PORT_BIND == 1U
    socket_shutdown(sender->rtp_socket, 1);
    #endif /* RTCP_PORT_BIND */
    socket_close(sender->rtp_socket);
  }
  if (sender->rtcp_socket >= 0)
  {
    #if RTCP_PORT_BIND == 1U
    socket_shutdown(sender->rtcp_socket, 1);
    #endif /* RTCP_PORT_BIND */
    socket_close(sender->rtcp_socket);
  }
  if (sender->rtcp_event != NULL)
  {
    vEventGroupDelete(sender->rtcp_event);
  }
  if (sender->mutex != NULL)
  {
    vSemaphoreDelete(sender->mutex);
  }
}

/**
 * @brief Find a session by SSRC in the sender's session list
 * @param sender Pointer to the RTP sender
 * @param session Pointer to store the found session
 * @param ssrc SSRC of the session to find
 * @return Error code
 */
static int32_t  _w6x_rtp_sender_session_find(
  w6x_rtp_sender_t  *sender,
  w6x_rtp_session_t **session,
  uint32_t          ssrc
)
{
  w6x_rtp_session_t *current = sender->session;
  while (current)
  {
    if (current->ssrc == ssrc)
    {
      *session = current;
      return RTP_OK;
    }
    current = current->next;
  }
  return RTP_ERROR_NOT_FOUND;
}

/**
 * @brief RTCP packet receive task
 * @param args Pointer to the RTP sender structure
 */
static void 	 	_w6x_rtcp_packet_rx_task(
  void              *args
)
{
  w6x_rtp_sender_t  *sender = (w6x_rtp_sender_t*)args;
  uint8_t           packet[UDP_PAYLOAD_MAX_SIZE];
  socklen_t         client_size;
  sockaddr_in_t     client;
  uint8_t           running;
  int32_t           status;

  /* Wait until sender signal */
  status  = (int32_t)xEventGroupWaitBits(sender->rtcp_event, RTCP_TASK_EVT_START | RTCP_TASK_EVT_ABORT, pdFALSE, pdFALSE, portMAX_DELAY);
  running = ((status & RTCP_TASK_EVT_ABORT) == 0) && ((status & RTCP_TASK_EVT_START) != 0);

  /* Run sender logic */
  while (running && (sender->id == RTP_SENDER_ID) && (sender->rtcp_socket >= 0))
  {
    /* Wait for a new packet */
    status = socket_recvfrom(sender->rtcp_socket, packet, UDP_PAYLOAD_MAX_SIZE, 0, (sockaddr_t*)&client, &client_size);
    if (status > 0)
    {
      if (sender->rtcp_raw_cb != NULL)
      {
        sender->rtcp_raw_cb(sender, &client, packet, (size_t)status);
      }
      if ((sender->rtcp_rr_cb != NULL) || (sender->rtcp_sdes_cb != NULL))
      {
        _w6x_rtcp_process_packet(sender, packet, (size_t)status);
      }
    }
  }

  /* Report task termination */
  xEventGroupSetBits(sender->rtcp_event, RTCP_TASK_EVT_COMPLETED);
  vTaskDelete(sender->rtcp_task);
}

/**
 * @brief Process an RTCP packet
 * @param sender Pointer to the RTP sender
 * @param packet Pointer to the RTCP packet data
 * @param size Size of the RTCP packet data
 * @return Error code
 */
static int32_t  _w6x_rtcp_process_packet(
  w6x_rtp_sender_t  *sender,
  uint8_t           *packet,
  size_t            size
)
{
  w6x_rtcp_pkt_header_t *header;
  w6x_rtcp_pkt_header_t *next;
  uint8_t               *end;
  int32_t               status;

  /* Validate */
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }

  /* Prepare */
  header = (w6x_rtcp_pkt_header_t*)packet;
  end    = packet + size;

  /* Validate size */
  if (((uint8_t*)header + sizeof(w6x_rtcp_pkt_header_t)) > end)
  {
    return RTP_ERROR_PACKET;
  }

  /* Validate header
   * 1) The Padding bit should be zero for the first packet of a compound RTCP packet
   * 2) The payload type field of the first RTCP packet in a compound packet must be equal to SR or RR
   */
  if (
    ((header->byte0 & RTCP_HEADER_INFO_PAD_MASK) != RTCP_HEADER_INFO_PAD) ||
    ((header->packet_type & RTCP_HEADER_TYPE_MASK) != RTCP_HEADER_TYPE_SR)
  )
  {
    return RTP_ERROR_PACKET;
  }

  /* Process packets */
  status = RTP_OK;
  do
  {
    /* Find next header
     * header->size is in 32-bit words, hence the use of (uint32_t*)
     */
    U16_ENDIAN_SWAP_IP(header->size);
    next = (w6x_rtcp_pkt_header_t*)((uint32_t*)header + header->size + 1U);

    /* Validate packet
     * 1) The version field must be equal to 2
     * 2) We should have enough bytes
     */
    if (
      ((header->byte0 & RTCP_HEADER_INFO_VERSION_MASK) != RTCP_HEADER_INFO_VERSION) ||
      ((uint8_t*)next > end)
    )
    {
      return RTP_ERROR_PACKET;
    }

    /* Process packet */
    switch (header->packet_type)
    {
      case RTCP_HEADER_TYPE_RR:
        status = _w6x_rtcp_process_packet_rr(sender, header);
        break;

      case RTCP_HEADER_TYPE_SDES:
        status = _w6x_rtcp_process_packet_sdes(sender, header);
        break;

      default:
        break;
    }
    if (status != RTP_OK)
    {
      break;
    }
    header = next;
  } while ((uint8_t*)header + sizeof(w6x_rtcp_pkt_header_t) <= end);
  return status;
}

/**
 * @brief Process an RTCP Receiver Report (RR) packet
 * @param sender Pointer to the RTP sender
 * @param header Pointer to the RTCP packet header
 * @return Error code
 */
static int32_t  _w6x_rtcp_process_packet_rr(
  w6x_rtp_sender_t      *sender,
  w6x_rtcp_pkt_header_t *header
)
{
  w6x_rtp_session_t *session;
  w6x_rtcp_rr_t     report;
  w6x_rtcp_pkt_rr_t *rr;

  /* Validate */
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }
  if (sender->rtcp_rr_cb == NULL)
  {
    /* No callback registered */
    return RTP_OK;
  }

  /* Check if valid packet */
  if (
    ((header->byte0 & RTCP_HEADER_INFO_COUNT_MASK) == 0)   ||        /* No items */
    ((header->size + 1U) < (sizeof(w6x_rtcp_pkt_rr_t) >> 2U))   /* No enough bytes */
  )
  {
    /* Allow RTCP to process other packet */
    return RTP_OK;
  }

  /* Get RR payload */
  rr = (w6x_rtcp_pkt_rr_t*)((uint8_t*)header + sizeof(w6x_rtcp_pkt_header_t));

  /* Acquire mutex */
  if (xSemaphoreTake(sender->mutex, 0) != pdTRUE)
  {
    return RTP_ERROR_RTOS;
  }

  /* If session available, process */
  U32_ENDIAN_SWAP_IP(rr->session);
  if (_w6x_rtp_sender_session_find(sender, &session, rr->session) == RTP_OK)
  {
    /* Fix endianness */
    U32_ENDIAN_SWAP_IP(rr->client);
    U32_ENDIAN_SWAP_IP(rr->loss);
    U32_ENDIAN_SWAP_IP(rr->extended_max);
    U32_ENDIAN_SWAP_IP(rr->jitter);
    U32_ENDIAN_SWAP_IP(rr->last_sr);
    U32_ENDIAN_SWAP_IP(rr->delay);

    /* Prepare for callback */
    report.client         = rr->client;
    report.fraction_loss  = rr->loss >> 24U;
    report.packet_loss    = (((int32_t)rr->loss << 8U) >> 8U);
    report.extended_max   = rr->extended_max;
    report.jitter         = rr->jitter;
    report.last_sr        = rr->last_sr;
    report.delay          = rr->delay;

    /* Invoke user callback */
    sender->rtcp_rr_cb(session, &report);
  }

  /* Release mutex and return */
  xSemaphoreGive(sender->mutex);
  return RTP_OK;
}

/**
 * @brief Process an RTCP Source Description (SDES) packet
 * @param sender Pointer to the RTP sender
 * @param header Pointer to the RTCP packet header
 * @return Error code
 */
static int32_t  _w6x_rtcp_process_packet_sdes(
  w6x_rtp_sender_t      *sender,
  w6x_rtcp_pkt_header_t *header
)
{
  w6x_rtcp_sdes_t           sdes;
  w6x_rtcp_pkt_sdes_chunk_t *chunk;
  w6x_rtcp_pkt_sdes_item_t  *item;
  uint8_t                   *end;
  int32_t                   count;

  /* Validate */
  if (sender->id != RTP_SENDER_ID)
  {
    return RTP_ERROR_PARAMETERS;
  }
  if (sender->rtcp_sdes_cb == NULL)
  {
    /* No callback registered */
    return RTP_OK;
  }

  /* Get SDES payload */
  count = (int32_t)(header->byte0 & RTCP_HEADER_INFO_COUNT_MASK);
  chunk = (w6x_rtcp_pkt_sdes_chunk_t*)((uint8_t*)header + sizeof(w6x_rtcp_pkt_header_t));
  end   = (uint8_t*)((uint32_t*)header + header->size + 1U);

  /* For each chunk... */
  while ((((uint8_t*)chunk + sizeof(w6x_rtcp_pkt_sdes_chunk_t)) < end) && (count-- > 0))
  {
    /* Fix endianness */
    U32_ENDIAN_SWAP_IP(chunk->source);

    /* Process all items... */
    item = &chunk->item[0];
    while ((((uint8_t*)item + sizeof(w6x_rtcp_pkt_sdes_item_t)) < end) && item->type)
    {
      /* Validate */
      if ((item->data + item->size) > end)
      {
        return RTP_ERROR_PACKET;
      }

      /* Prepare and invoke callback */
      if (item->type == RTCP_SDES_TYPE_CNAME)
      {
        /* Copy values */
        sdes.client     = chunk->source;
        sdes.cname      = item->data;
        sdes.cname_size = item->size;

        /* Invoke callback to process data */
        sender->rtcp_sdes_cb(&sdes);
      }

      /* Go to next item */
      item = (w6x_rtcp_pkt_sdes_item_t*)((uint8_t*)item + item->size + 2U);
    }

    /* RFC 3550, chapter 6.5.
     * The list of items in each chunk MUST be terminated by one or more null octets,
     * the first of which is interpreted as an item type of zero to denote the end
     * of the list.
     */
    chunk = (w6x_rtcp_pkt_sdes_chunk_t*)((uint8_t*)chunk  + (((uint8_t*)item - (uint8_t*)chunk) >> 2U) + 1U);
  }
  return RTP_OK;
}

/**
 * @brief Send an RTCP packet
 * @param session Pointer to the RTP session
 * @return Error code
 */
static int32_t  _w6x_rtcp_send_packet(
  w6x_rtp_session_t *session
)
{
  uint8_t   packet[RTCP_PACKET_SIZE] = { 0 };
  uint32_t  timestamp = xTaskGetTickCount();
  int32_t   status;

  /* Validate */
  if (session->rtcp_timestamp && ((timestamp - session->rtcp_timestamp) < RTCP_INTERVAL))
  {
    return RTP_OK;
  }

  /* Prepare packet */
  uint8_t       *head = packet;
  const uint8_t *tail = packet + sizeof(packet);
  status = _w6x_rtcp_append_packet_sr(session, &head, tail);
  if (status != RTP_OK)
  {
    return status;
  }
  status = _w6x_rtcp_append_packet_sdes(session, &head, tail);
  if (status != RTP_OK)
  {
    return status;
  }

  /* Send packet */
  status = socket_sendto(
    session->sender->rtcp_socket, packet, (size_t)(head - packet), 0,
    (const sockaddr_t*)&session->client_rtcp, sizeof(session->client_rtcp)
  );
  if (status < 0)
  {
    return RTP_ERROR_SOCKET;
  }

  /* Update variables */
  session->rtcp_timestamp = timestamp;
  return RTP_OK;
}

/**
 * @brief Append an RTCP SDES packet to the buffer
 * @param session Pointer to the RTP session
 * @param head Pointer to the head of the buffer where the packet will be appended
 * @param tail Pointer to the tail of the buffer where the packet can be appended
 * @return Error code
 */
static int32_t  _w6x_rtcp_append_packet_sdes(
  w6x_rtp_session_t *session,
  uint8_t           **head,
  const uint8_t     *tail
)
{
  /* Packet size: header + SDES chunk (SSRC + TYPE + SIZE + PLACEHOLDER) + CNAME - PLACEHOLDER */
  size_t  size      = sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sdes_chunk_t) + session->sender->cname_size - 1U;
  size_t  required  = U32_ALIGN(size);
  size_t  available = tail - *head;

  /* Validate */
  if (available < required)
  {
    return RTP_ERROR_PACKET;
  }

  /* Set header */
  w6x_rtcp_pkt_header_t *header = (w6x_rtcp_pkt_header_t*)*head;
  header->byte0       = RTCP_HEADER_INFO_VERSION | 1U;          /* 1 SDES item */
  header->packet_type = RTCP_HEADER_TYPE_SDES;
  header->size        = (uint16_t)(required >> 2U);

  /* Set SDES */
  w6x_rtcp_pkt_sdes_chunk_t *chunk = (w6x_rtcp_pkt_sdes_chunk_t*)(*head + sizeof(w6x_rtcp_pkt_header_t));
  chunk->source       = session->ssrc;
  chunk->item[0].type = RTCP_SDES_TYPE_CNAME;
  chunk->item[0].size = session->sender->cname_size;
  memcpy(chunk->item[0].data, session->sender->cname, session->sender->cname_size);

  /* Fix endianness */
  U16_ENDIAN_SWAP_IP(header->size);
  U32_ENDIAN_SWAP_IP(chunk->source);

  /* Update head pointer (add padding if required) */
  *head += size;
  memset(*head, RTCP_PAD_VALUE, (required - size));
  *head += (required - size);
  return RTP_OK;
}

/**
 * @brief Append an RTCP Sender Report (SR) packet to the buffer
 * @param session Pointer to the RTP session
 * @param head Pointer to the head of the buffer where the packet will be appended
 * @param tail Pointer to the tail of the buffer where the packet can be appended
 * @return Error code
 */
static int32_t  _w6x_rtcp_append_packet_sr(
  w6x_rtp_session_t *session,
  uint8_t           **head,
  const uint8_t     *tail
)
{
  /* Packet size: header + SR payload */
  size_t  size      = sizeof(w6x_rtcp_pkt_header_t) + sizeof(w6x_rtcp_pkt_sr_t);
  size_t  required  = U32_ALIGN(size);
  size_t  available = tail - *head;

  /* Validate */
  if (available < required)
  {
    return RTP_ERROR_PACKET;
  }

  /* Set header */
  w6x_rtcp_pkt_header_t *header = (w6x_rtcp_pkt_header_t*)*head;
  header->byte0       = RTCP_HEADER_INFO_VERSION;
  header->packet_type = RTCP_HEADER_TYPE_SR;
  header->size        = (uint16_t)(required >> 2U);

  /* Set SR payload */
  w6x_rtcp_pkt_sr_t *sr = (w6x_rtcp_pkt_sr_t*)(*head + sizeof(w6x_rtcp_pkt_header_t));
  sr->session         = session->ssrc;
  sr->ntp_msw         = session->ntp_msw;
  sr->ntp_lsw         = session->ntp_lsw;
  sr->rtp_timestamp   = session->rtp_timestamp;
  sr->packet_count    = session->packet_count;
  sr->octet_count     = session->octet_count;

  /* Fix endianness */
  U16_ENDIAN_SWAP_IP(header->size);
  U32_ENDIAN_SWAP_IP(sr->session);
  U32_ENDIAN_SWAP_IP(sr->ntp_msw);
  U32_ENDIAN_SWAP_IP(sr->ntp_lsw);
  U32_ENDIAN_SWAP_IP(sr->rtp_timestamp);
  U32_ENDIAN_SWAP_IP(sr->packet_count);
  U32_ENDIAN_SWAP_IP(sr->octet_count);

  /* Update head pointer (add padding if required) */
  *head += size;
  memset(*head, RTCP_PAD_VALUE, (required - size));
  *head += (required - size);
  return RTP_OK;
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Component -->
* @} <!-- End: W6X_RTP_Sender -->
*//*--------------------------------------------------------------------------*/
