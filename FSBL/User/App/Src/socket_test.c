/**
 *******************************************************************************
 * @file    socket_test.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   LWIP socket testing
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */

#include "w6x_rtp_platform.h"

/* Private tunables ----------------------------------------------------------*/

/* Private definitions -------------------------------------------------------*/

#define TEST_PORT_BASE          5001U
#define TEST_TIMEOUT            5000U
#define TEST_TASK_PRIO          ((configMAX_PRIORITIES / 2U) - 1U)
#define TEST_TASK_STACK_SIZE    (2048U + UDP_PAYLOAD_MAX_SIZE)

/* Private macros ------------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

typedef struct
{
  TaskHandle_t  handle;
  int32_t       socket;
  uint16_t      port;
  uint8_t       packet[UDP_PAYLOAD_MAX_SIZE];
  char          *name;
} t_server;

/* Private data --------------------------------------------------------------*/

static t_server _server[] = {
  { NULL, -1, TEST_PORT_BASE + 0U, { 0 }, "task.lbtest1" },
  { NULL, -1, TEST_PORT_BASE + 1U, { 0 }, "task.lbtest2" },
  { NULL, -1, TEST_PORT_BASE + 2U, { 0 }, "task.lbtest3" },
  { NULL },
};

/* Private function ----------------------------------------------------------*/

static void    _socket_loopback_test_task(void*args);
extern int32_t w6x_socket_udp_create(uint8_t bind, uint16_t port);

/* Public API definitions ----------------------------------------------------*/

int32_t lwip_socket_test(void)
{
  t_server *item = _server;
  int32_t  status;

  /* Create tasks */
  while (item->name != NULL)
  {
    status = xTaskCreate(
      _socket_loopback_test_task, item->name,
      TEST_TASK_STACK_SIZE / sizeof(StackType_t),
      item, TEST_TASK_PRIO, &item->handle
    );
    if (status != pdPASS)
    {
      LogError("Failed to create task %s (%" PRIi32 ")\n", item->name, status);
      return -1;
    }
    item++;
  }

  /* Wait forever */
  while (1U)
  {
    vTaskDelay(pdMS_TO_TICKS(1000U));
  }
}

/* Private function definitions ----------------------------------------------*/

static void _socket_loopback_test_task(void *args)
{
  t_server      *server = (t_server*)args;
  sockaddr_in_t client;
  socklen_t     csize;
  int32_t       psize;

  /* Start server */
  server->socket = w6x_socket_udp_create(true, server->port);
  if (server->socket < 0)
  {
    LogError("Failed to create UDP socket, %" PRIi32 "\n", server->socket);
    return;
  }

  /* Run loopback */
  while (1U)
  {
    /* Receive message */
    psize = recvfrom(server->socket, server->packet, UDP_PAYLOAD_MAX_SIZE, 0, (sockaddr_t*)&client, &csize);
    if (psize > 0)
    {
      sendto(server->socket, server->packet, psize, 0, (sockaddr_t*)&client, csize);
      LogInfo("[%d] UDP RTX: %s\n", server->socket, server->packet);
      memset(server->packet, 0, UDP_PAYLOAD_MAX_SIZE);
    }
  }
}
