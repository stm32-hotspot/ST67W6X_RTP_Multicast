/**
 ******************************************************************************
 * @file    w6x_rtp_sender.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines the API for the ST67 RTP sender.
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
#ifndef _W6X_RTP_SENDER_H_
#define _W6X_RTP_SENDER_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include "w6x_rtp_platform.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Component
* @{
* @addtogroup W6X_RTP_Sender
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/* RTP constants */
#define RTP_VERSION                     2U
#define RTP_PORT                        5004U

#define RTP_CNAME_MAX_SIZE              32U
#define RTP_PAYLOAD_MAX_TYPE            127U

/* RTP identifiers */
#define RTP_SENDER_ID                   0x52545053UL          /* RTPS */
#define RTP_SESSION_ID                  0x52545054UL          /* RTPT */

/* RTP error codes */
#define RTP_OK                           0
#define RTP_ERROR_PTR                   -1
#define RTP_ERROR_RTOS                  -2
#define RTP_ERROR_PARAMETERS            -3
#define RTP_ERROR_PACKET                -4
#define RTP_ERROR_SOCKET                -5
#define RTP_ERROR_NOT_FOUND             -6

/* RTP packet definitions */
#define RTP_HEADER_MARKER               0x80U
#define RTP_HEADER_VERSION              (RTP_VERSION << 6U)

/* RTCP packet definitions */
#define RTCP_PAD_VALUE                  0x00U
#define RTCP_INTERVAL                   5000U                 /* RFC3550_CH6.25s: Minimum recommended interval 5[s] */

#define RTCP_HEADER_INFO_COUNT_MASK     0x1FU
#define RTCP_HEADER_INFO_PAD_MASK       0x20U
#define RTCP_HEADER_INFO_PAD            0x00U
#define RTCP_HEADER_INFO_VERSION_MASK   0xC0U
#define RTCP_HEADER_INFO_VERSION        RTP_HEADER_VERSION
#define RTCP_HEADER_TYPE_MASK           0xFEU
#define RTCP_HEADER_TYPE_SR             200U
#define RTCP_HEADER_TYPE_RR             201U
#define RTCP_HEADER_TYPE_SDES           202U

#define RTCP_SDES_TYPE_CNAME            1U

/* RTP H264 definitions */
#define RTP_H264_NRI_MASK               0x60U
#define RTP_H264_TYPE_MASK              0x1FU
#define RTP_H264_TYPE_SEI               6U
#define RTP_H264_TYPE_SPS               7U
#define RTP_H264_TYPE_PPS               8U
#define RTP_H264_TYPE_FU_A              28U
#define RTP_H264_FU_START_MASK          0x80U
#define RTP_H264_FU_END_MASK            0x40U
#define RTP_H264_FU_SIZE                2U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

typedef struct _w6x_rtp_sender_s  w6x_rtp_sender_t;
typedef struct _w6x_rtp_session_s w6x_rtp_session_t;

/** RTP packet header */
typedef struct
{
  uint8_t                   byte0;            /* V(0-1), P(2), X(3), CC(4-7)*/
  uint8_t                   byte1;            /* M(0), PT(1-7) */
  uint16_t                  sequence_number;  /* Session sequence number */
  uint32_t                  rtp_timestamp;    /* RTP packet timestamp */
  uint32_t                  session;          /* Session SSRC */
} w6x_rtp_pkt_header_t;

/** RTCP packet header */
typedef struct
{
  uint8_t                   byte0;            /* V(0-1), P(2), RC(3-7) */
  uint8_t                   packet_type;      /* Packet type */
  uint16_t                  size;             /* Size in words */
} w6x_rtcp_pkt_header_t;

/** RTCP receiver report payload */
typedef struct
{
  uint32_t                  client;           /* Client SSRC (Receiver) */
  uint32_t                  session;          /* Source SSRC (Reported) */
  uint32_t                  loss;             /* Fraction + cumulative packet loss */
  uint32_t                  extended_max;     /* Extended max sequence number received */
  uint32_t                  jitter;           /* Inter-arrival time */
  uint32_t                  last_sr;          /* Middle 32 bits of NTP timestamp */
  uint32_t                  delay;            /* Delay since last SR timestamp */
} w6x_rtcp_pkt_rr_t;

/* RTCP SDES item */
typedef struct
{
  uint8_t                   type;             /* SDES type */
  uint8_t                   size;             /* Size in bytes */
  uint8_t                   data[1U];         /* Data */
} w6x_rtcp_pkt_sdes_item_t;

/** RTCP SDES chunk */
typedef struct
{
  uint32_t                  source;           /* SDES source */
  w6x_rtcp_pkt_sdes_item_t  item[1U];         /* SDES items */
} w6x_rtcp_pkt_sdes_chunk_t;

/** RTCP receiver report payload */
typedef struct
{
  uint32_t                  session;          /* Session SSRC */
  uint32_t                  ntp_msw;          /* NTP timestamp MSW */
  uint32_t                  ntp_lsw;          /* NTP timestamp LSW */
  uint32_t                  rtp_timestamp;    /* Timestamp of last RTP packet sent */
  uint32_t                  packet_count;     /* Total number of RTP packets sent */
  uint32_t                  octet_count;      /* Total number of payload octets sent */
} w6x_rtcp_pkt_sr_t;

/** RTCP receiver report */
typedef struct
{
  uint32_t                  client;         /* Client ID */
  uint32_t                  fraction_loss;  /* Fraction of packets lost */
  int32_t                   packet_loss;    /* Cumulative number of packets lost */
  uint32_t                  extended_max;   /* Extended max sequence number received */
  uint32_t                  jitter;         /* Inter-arrival time */
  uint32_t                  last_sr;        /* Last SR timestamp */
  uint32_t                  delay;          /* Delay since last SR */
} w6x_rtcp_rr_t;

/** RTCP SDES */
typedef struct
{
  uint32_t                  client;         /* Client ID */
  uint8_t                   *cname;         /* Canonical name */
  uint32_t                  cname_size;     /* Size of cname */
} w6x_rtcp_sdes_t;

/* RTCP callbacks */
typedef int32_t (*w6x_rtcp_raw_cb)(w6x_rtp_sender_t *sender, sockaddr_in_t *client, uint8_t *packet, size_t size);
typedef int32_t (*w6x_rtcp_rr_cb)(w6x_rtp_session_t *session, w6x_rtcp_rr_t *report);
typedef int32_t (*w6x_rtcp_sdes_cb)(w6x_rtcp_sdes_t *info);

/** RTP sender instance */
struct _w6x_rtp_sender_s
{
  uint32_t                  id;
  uint16_t                  port;
  uint8_t                   cname[RTP_CNAME_MAX_SIZE];
  uint8_t                   cname_size;
  w6x_rtp_session_t         *session;
  SemaphoreHandle_t         mutex;

  /* RTP support */
  int16_t                   rtp_socket;
  uint8_t                   rtp_packet[UDP_PAYLOAD_MAX_SIZE];
  size_t                    rtp_payload_max_size;

  /* RTCP support */
  int16_t                   rtcp_socket;
  uint8_t                   rtcp_task_name[RTP_CNAME_MAX_SIZE];
  TaskHandle_t              rtcp_task;
  EventGroupHandle_t        rtcp_event;
  w6x_rtcp_raw_cb           rtcp_raw_cb;
  w6x_rtcp_rr_cb            rtcp_rr_cb;
  w6x_rtcp_sdes_cb          rtcp_sdes_cb;
};

/** RTP session instance */
struct _w6x_rtp_session_s
{
  uint32_t                  id;
  uint32_t                  ssrc;             /* Session SSRC */
  w6x_rtp_sender_t          *sender;          /* Associated RTP sender */
  w6x_rtp_session_t         *next;            /* Next session in the list */

  /* RTP header */
  uint8_t                   payload_type;     /* RTP payload type */
  uint16_t                  sequence_number;  /* Session sequence number */
  uint32_t                  sample_factor;    /* Encoding: 0 frame-based, non-zero sample-based */

  /* RTCP statistics */
  uint32_t                  ntp_msw;          /* NTP timestamp MSW */
  uint32_t                  ntp_lsw;          /* NTP timestamp LSW */
  uint32_t                  rtp_timestamp;    /* Timestamp of last RTP packet sent */
  uint32_t                  rtcp_timestamp;   /* Timestamp of last RTCP packet sent */
  uint32_t                  packet_count;     /* Total number of RTP packets sent */
  uint32_t                  octet_count;      /* Total number of payload octets sent */

  /* Client info */
  sockaddr_in_t             client_rtp;       /* Client RTP address */
  sockaddr_in_t             client_rtcp;      /* Client RTCP address */
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Create a new RTP sender instance
 * @param sender Pointer to the RTP sender
 * @param cname Pointer to the canonical name (CNAME) string
 * @param port RTP port number (even number)
 * @return Error code
 */
int32_t w6x_rtp_sender_create(
  w6x_rtp_sender_t  *sender,
  uint8_t           *cname,
  uint16_t          port
);

/**
 * @brief Get the RTP and RTCP ports of the sender
 * @param sender Pointer to the RTP sender
 * @param rtp_port Pointer to store the RTP port number
 * @param rtcp_port Pointer to store the RTCP port number
 * @return Error code
 */
int32_t w6x_rtp_sender_get_port(
  w6x_rtp_sender_t  *sender,
  uint16_t          *rtp_port,
  uint16_t          *rtcp_port
);

/**
 * @brief Set the RTCP Receiver Report (RR) callback
 * @param sender Pointer to the RTP sender
 * @param callback Pointer to the RTCP RR callback function
 * @return Error code
 */
int32_t w6x_rtp_sender_set_rtcp_rr_callback(
  w6x_rtp_sender_t  *sender,
  w6x_rtcp_rr_cb    callback
);

/**
 * @brief Set the RTCP Source Description (SDES) callback
 * @param sender Pointer to the RTP sender
 * @param callback Pointer to the RTCP SDES callback function
 * @return Error code
 */
int32_t w6x_rtp_sender_set_rtcp_sdes_callback(
  w6x_rtp_sender_t  *sender,
  w6x_rtcp_sdes_cb  callback
);

/**
 * @brief Delete an RTP sender instance
 * @param sender Pointer to the RTP sender
 * @return Error code
 */
int32_t w6x_rtp_sender_delete(
  w6x_rtp_sender_t  *sender
);

/**
 * @brief Create a new RTP session
 * @param sender Pointer to the RTP sender
 * @param session Pointer to the RTP session to create
 * @param payload_type RTP payload type (0-127)
 * @param rtp_port RTP port number (even number)
 * @param rtcp_port RTCP port number (odd number, RTP port + 1)
 * @param ip IP address of the client
 * @return Error code
 */
int32_t w6x_rtp_sender_session_create(
  w6x_rtp_sender_t  *sender,
  w6x_rtp_session_t *session,
  uint16_t          payload_type,
  uint16_t          rtp_port,
  uint16_t          rtcp_port,
  in_addr_t         ip
);

/**
 * @brief Get the SSRC of an RTP session
 * @param session Pointer to the RTP session
 * @param ssrc Pointer to store the SSRC
 * @return Error code
 */
int32_t w6x_rtp_sender_session_get_ssrc(
  w6x_rtp_session_t *session,
  uint32_t          *ssrc
);

/**
 * @brief Get the sequence number of an RTP session
 * @param session Pointer to the RTP session
 * @param number Pointer to store the sequence number
 * @return Error code
 */
int32_t w6x_rtp_sender_session_get_sequence_number(
  w6x_rtp_session_t *session,
  uint16_t          *number
);

/**
 * @brief Set the sample factor for an RTP session.
 * @param session Pointer to the RTP session
 * @param factor Sample factor to set (0 for frame-based, non-zero for sample-based)
 * @return Error code
 */
int32_t w6x_rtp_sender_session_set_sample_factor(
  w6x_rtp_session_t *session,
  uint32_t          factor
);

/**
 * @brief Send a raw RTP packet
 * @param session Pointer to the RTP session
 * @param data Pointer to the RTP packet data
 * @param size Size of the RTP packet data
 * @param timestamp RTP timestamp for the packet
 * @param ntp_msw NTP timestamp MSW
 * @param ntp_lsw NTP timestamp LSW
 * @param marker RTP marker bit (should be 1 for full frames)
 * @return Error code
 */
int32_t w6x_rtp_sender_session_send_packet(
  w6x_rtp_session_t *session,
  uint8_t           *data,
  size_t            size,
  uint32_t          timestamp,
  uint32_t          ntp_msw,
  uint32_t          ntp_lsw,
  uint8_t           marker
);

/**
 * @brief Send an H264 frame
 * @param session Pointer to the RTP session
 * @param frame Pointer to the H264 frame data
 * @param size Size of the H264 frame data
 * @param timestamp RTP timestamp for the frame
 * @param ntp_msw NTP timestamp MSW
 * @param ntp_lsw NTP timestamp LSW
 * @param marker RTP marker bit (should be 1 for full frames)
 * @return Error code
 */
int32_t w6x_rtp_sender_session_send_h264(
  w6x_rtp_session_t *session,
  uint8_t           *frame,
  size_t            size,
  uint32_t          timestamp,
  uint32_t          ntp_msw,
  uint32_t          ntp_lsw,
  uint8_t           marker
);

/**
 * @brief Delete an RTP session
 * @param session Pointer to the RTP session
 * @return Error code
 */
int32_t w6x_rtp_sender_session_delete(
  w6x_rtp_session_t *session
);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Component -->
* @} <!-- End: W6X_RTP_Sender -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _W6X_RTP_SENDER_H_ */
