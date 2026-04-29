/**
 *******************************************************************************
 * @file    w6x_rtp_platform.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   Defines types and macros for the ST67 RTP sender API.
 *******************************************************************************
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
 *******************************************************************************
 */
#ifndef _W6X_RTP_PLATFORM_H_
#define _W6X_RTP_PLATFORM_H_
#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* RTOS */
#include "FreeRTOS.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"

/* Driver */
#include "w6x_api.h"
#if ST67_ARCH == W6X_ARCH_T02
  #include "lwip.h"
  #include "lwip/sockets.h"
#endif /* ST67_ARCH */

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Component
* @{
* @addtogroup W6X_RTP_Platform
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Address Definitions <<------------------*/
#ifndef IPADDR_ANY
  /** 0.0.0.0 */
  #define IPADDR_ANY            0x00000000UL
#endif /* IPADDR_ANY */

/*-->> Packet Definitions <<-------------------*/
#ifndef MTU_SIZE
  /** Maximum Transmission Unit */
  #define MTU_SIZE              1200U
#endif /* MTU_SIZE */

#ifndef IP_HEADER_SIZE
  /** IPv4 header size */
  #define IP_HEADER_SIZE        20U
#endif /* IP_HEADER_SIZE */

#ifndef UDP_HEADER_SIZE
  /** UDP header size */
  #define UDP_HEADER_SIZE       8U
#endif /* UDP_HEADER_SIZE */

#ifndef UDP_PAYLOAD_MAX_SIZE
  /** Maximum UDP payload size */
  #define UDP_PAYLOAD_MAX_SIZE  (MTU_SIZE - IP_HEADER_SIZE - UDP_HEADER_SIZE)
#endif /* UDP_PAYLOAD_MAX_SIZE */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Socket Macros <<------------------------*/
#if ST67_ARCH == W6X_ARCH_T01
  #define socket_create         W6X_Net_Socket
  #define socket_setopt         W6X_Net_Setsockopt
  #define socket_bind           W6X_Net_Bind
  #define socket_recvfrom       W6X_Net_Recvfrom
  #define socket_sendto         W6X_Net_Sendto
  #define socket_close          W6X_Net_Close
  #define socket_shutdown       W6X_Net_Shutdown
#else
  #define socket_create         socket
  #define socket_setopt         setsockopt
  #define socket_bind           bind
  #define socket_recvfrom       recvfrom
  #define socket_sendto         sendto
  #define socket_close          close
  #define socket_shutdown       shutdown
#endif /* ST67_ARCH */

/*-->> Logging Macros <<-----------------------*/
#define RTP_LERROR(...)         LogError(__VA_ARGS__)
#define RTP_LWARNING(...)       LogWarn(__VA_ARGS__)
#define RTP_LINFO(...)          LogInfo(__VA_ARGS__)
#define RTP_LDEBUG(...)         LogDebug(__VA_ARGS__)

/*-->> Byte Manipulation <<--------------------*/
#ifndef BIT
  /** Create a bit-mask with bit x set */
  #define BIT(x)                (1U << (x))
#endif /* BIT */

#ifndef U16_ALIGN
  /** Align a value to the next multiple of 2 */
  #define U16_ALIGN(x)          (((x) + 1U) & ~1U)
#endif /* U16_ALIGN */

#ifndef U32_ALIGN
  /** Align a value to the next multiple of 4 */
  #define U32_ALIGN(x)          (((x) + 3U) & ~3U)
#endif /* U32_ALIGN */

#ifndef U16_ENDIAN_SWAP
  /** Swap the endianness of a 16-bit value */
  #define U16_ENDIAN_SWAP(x)    __builtin_bswap16(x)
  #define U16_ENDIAN_SWAP_IP(x) (x) = U16_ENDIAN_SWAP(x)
#endif /* U16_ENDIAN_SWAP */

#ifndef U32_ENDIAN_SWAP
  /** Swap the endianness of a 32-bit value */
  #define U32_ENDIAN_SWAP(x)    __builtin_bswap32(x)
  #define U32_ENDIAN_SWAP_IP(x) (x) = U32_ENDIAN_SWAP(x)
#endif /* U32_ENDIAN_SWAP */

#ifndef htons
  /** Host to network short */
  #define htons(x)              U16_ENDIAN_SWAP(x)
#endif /* htons */

#ifndef htonl
  /** Host to network long */
  #define htonl(x)              U32_ENDIAN_SWAP(x)
#endif /* htonl */

#if ST67_ARCH == W6X_ARCH_T01
  #define SET_TIMEVAL_IP(v, t)  { (v) = (t); }
#else
  #define SET_TIMEVAL_IP(v, t)  { (v).tv_sec = (t) / 1000U; (v).tv_usec = ((t) % 1000U) * 1000U; }
#endif

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Typedef Helpers <<----------------------*/
#if ST67_ARCH == W6X_ARCH_T01
typedef uint32_t                in_addr_t;
typedef int32_t                 timeval_t;
#else
typedef struct timeval          timeval_t;
#endif /* ST67_ARCH */

typedef struct sockaddr         sockaddr_t;
typedef struct sockaddr_in      sockaddr_in_t;
typedef struct sockaddr_storage sockaddr_storage_t;

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Component -->
* @} <!-- End: W6X_RTP_Platform -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _W6X_RTP_PLATFORM_H_ */
