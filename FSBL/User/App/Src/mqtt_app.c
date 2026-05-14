/**
 *******************************************************************************
 * @file    app_mqtt.c
 * @author  STMicroelectronics
 * @date    2026
 * @brief   MQTT telemetry and subscription module
 *******************************************************************************
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "mqtt_app.h"
#include "logging.h"
#include "main.h"
#include "mqtt.h"
#include "sntp.h"
#include "altls_mbedtls.h"
#include "cJSON.h"
#include "w6x_api.h"

#include "common_parser.h"


#if ST67_ARCH == W6X_ARCH_T02
#include "lwip.h"
#include "lwip/sockets.h"
#include <lwip/api.h>
#include <lwip/errno.h>
#include <netdb.h>
#include "dns.h"
#endif /* ST67_ARCH */

#ifndef REDEFINE_FREERTOS_INTERFACE
#include "app_freertos.h"
#include "queue.h"
#include "event_groups.h"
#include "semphr.h"
#include "task.h"
#endif /* REDEFINE_FREERTOS_INTERFACE */

/* External RTC handle if available */
extern RTC_HandleTypeDef hrtc;

/* MQTT config ---------------------------------------------------------------*/

#ifndef SNTP_TIMEZONE
#define SNTP_TIMEZONE                   1
#endif

#ifndef MQTT_HOST_NAME
#define MQTT_HOST_NAME                  "broker.emqx.io"
#endif

#ifndef MQTT_HOST_PORT
#define MQTT_HOST_PORT                  8883 // 1883
#endif

#ifndef MQTT_SECURITY_LEVEL
#define MQTT_SECURITY_LEVEL             1
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID                  "mySTM32_772"
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME                   "Username"
#endif

#ifndef MQTT_USER_PASSWORD
#define MQTT_USER_PASSWORD              "Password"
#endif

#ifndef MQTT_CERTIFICATE
#define MQTT_CERTIFICATE                "client_mqtt.crt"
#endif

#ifndef MQTT_KEY
#define MQTT_KEY                        "client_mqtt.key"
#endif

#ifndef MQTT_CA_CERTIFICATE
#define MQTT_CA_CERTIFICATE             "ca_mqtt.crt"
#endif

#ifndef MQTT_SNI
#define MQTT_SNI                        "broker.emqx.io" // "server.local"
#endif

#ifndef MQTT_KEEP_ALIVE
#define MQTT_KEEP_ALIVE                 120
#endif

#ifndef MQTT_DIS_CLEAN_SESSION
#define MQTT_DIS_CLEAN_SESSION          0
#endif

#define SUBSCRIPTION_THREAD_PRIO        26
#define SUBSCRIPTION_TASK_STACK_SIZE    1024U
#define REFRESHER_THREAD_PRIO           14
#define REFRESHER_TASK_STACK_SIZE       4096U
#define MQTT_THREAD_PRIO                18
#define MQTT_TASK_STACK_SIZE            4096U

#define DNS_RESOLVE_TIMEOUT_MS          5000U

/* Private helpers -----------------------------------------------------------*/

#define APP_TICKS_TO_MS(t)              ((uint32_t)(((uint64_t)(t) * 1000U) / configTICK_RATE_HZ))

/* Private types -------------------------------------------------------------*/

typedef struct
{
  uint8_t *topic;
  uint32_t topic_length;
  uint8_t *message;
  uint32_t message_length;
} APP_MQTT_Data_t;

typedef struct
{
  int32_t res;
  ip_addr_t ipaddr;
} APP_DNS_res_t;

/* Private data --------------------------------------------------------------*/

static APP_Context_t *s_ctx = NULL;
static struct mqtt_client s_client = {0};

/** MQTT Broker connection configuration */
static W6X_MQTT_Connect_t mqtt_config =
{
  .HostName = MQTT_HOST_NAME,         /*!< Host name of remote MQTT Broker */
  .HostPort = MQTT_HOST_PORT,         /*!< Port of remote MQTT Broker */
  .MQClientId = MQTT_CLIENT_ID,       /*!< MQTT Client ID to be identified on MQTT Broker */
  /** Security level
    * 0: No security (TCP connection)
    * 1: SSL with Username/Password authentication
    * 2: SSL with Server certificate (CACertificate)
    * 3: SSL with Client certificate (Certificate and PrivateKey)
    * 4: SSL with both certificates (CACertificate, Certificate, PrivateKey) */
  .Scheme = MQTT_SECURITY_LEVEL,
  /** MQTT Username to be identified on MQTT Broker
    * Required when the scheme is greater or equal to 1 */
  .MQUserName = MQTT_USERNAME,
  /** MQTT Password to be identified on MQTT Broker
    * Required when the scheme is greater or equal to 1 */
  .MQUserPwd = MQTT_USER_PASSWORD,
  /** CA certificate
    * Required when the scheme is greater or equal to 2 */
  .CACertificateName = MQTT_CA_CERTIFICATE,
  /** Client Certificate
    * Required when the scheme is greater or equal to 3 */
  .CertificateName = MQTT_CERTIFICATE,
  /** Client Private key
    * Required when the scheme is greater or equal to 3 */
  .PrivateKeyName = MQTT_KEY,
  /** Server Name Indication (SNI) */
  .SNI = MQTT_SNI,
  /** Keep Alive interval using MQTT ping. Range [0, 7200]. 0 is forced to 120 */
  .KeepAlive = MQTT_KEEP_ALIVE,
  /** Skip cleaning the MQTT session */
  .DisableCleanSession = MQTT_DIS_CLEAN_SESSION,
  /** Last Will and Testament (LWT) topic */
  .WillTopic = "",
  /** LWT message */
  .WillMessage = "",
  /** LWT QoS. Range [0, 2] */
  .WillQos = 0,
  /** LWT Retain flag */
  .WillRetain = 0
};

#if (LFS_ENABLE == 0) && (MQTT_SECURITY_LEVEL != 0)
static const char ca_certificate[] =
  "-----BEGIN CERTIFICATE-----\r\n"
  "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\r\n"
  "Qw==\r\n"
  "-----END CERTIFICATE-----\r\n";

/** Client Certificate content */
static const char client_certificate[] =
  "-----BEGIN CERTIFICATE-----\r\n"
  "MIIEkTCCAnmgAwIBAgIUd2CXKip9OHFjOWnIoM7yJuNg2EYwDQYJKoZIhvcNAQEL\r\n"
  "BQAwXTELMAkGA1UEBhMCRlIxDjAMBgNVBAcMBVBhcmlzMRswGQYDVQQKDBJTVE1p\r\n"
  "Y3JvZWxlY3Ryb25pY3MxDzANBgNVBAsMBlJvb3RDQTEQMA4GA1UEAwwHUm9vdCBD\r\n"
  "QTAgFw0yNjAyMjcxMjUyMzNaGA8yMDU2MDQxMDEyNTIzM1owYjELMAkGA1UEBhMC\r\n"
  "RlIxDjAMBgNVBAcMBVBhcmlzMRswGQYDVQQKDBJTVE1pY3JvZWxlY3Ryb25pY3Mx\r\n"
  "DzANBgNVBAsMBkNsaWVudDEVMBMGA1UEAwwMY2xpZW50LmxvY2FsMIIBIjANBgkq\r\n"
  "hkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAlfoN+NpkCNoAmqwn6CmJORyOmSzbEnFK\r\n"
  "5qlLzJzAWKYfgHQvBoW3X5JL4pBVJTPRt0JvIJxnU77I4rtQcsBcN5JK6k/L6z71\r\n"
  "P/Ikc4UDRGRPkLcRwJKf+msUxKRGhIYN3CE55feUOCB3LF/a426pXEeBJELbxEA6\r\n"
  "an5pxf3emNM22Vvz3AJ23bv2qRu8Rc7fQ71lZSgwiwbzax+qshJMJ3gu0YCsxml+\r\n"
  "5sZqFHghRPfV0SXFDBa2jAmg/mRP3dBm2uUhtniTftGdfG9V4gH2N5hPFXWZY4rz\r\n"
  "Y2gjld2OTMNHBKhZ2YIFNam4VXRGzrWqA1sBf2GnHUM+2I+nQLofXQIDAQABo0Iw\r\n"
  "QDAdBgNVHQ4EFgQUG2+/+goza2iQYjK/uFlS5Ehnx/MwHwYDVR0jBBgwFoAUE5/a\r\n"
  "8NV6E1Aurf4eZxtzQbgu56EwDQYJKoZIhvcNAQELBQADggIBAAs7e8ioaDIOvKtF\r\n"
  "uYcBxl+E7kD4tUn1GCEWQSRxavYl97IjtfabRRskDJwKzB07UdCnkpviTyeJ7Ga2\r\n"
  "anJNevhBRbbSMLPIjMw01RJhVcAxa13g8K63PpTZy4WtkBaMNQHzlQzPEpn9Z/Ip\r\n"
  "vB4TgdSrFOoWUdl+a3xoRlGXF14r7c+kzLS7/N1x0X9D6lQcNqw1oTtvfMgZUBX1\r\n"
  "F31hY3nYrP0+etkr8nZtvYBysByXCn/wcIb8ARr4qj39c45zKnOC4hhqLh40QUfA\r\n"
  "rK6BU6TFzJUpG9Br5ku1czpwR+ZRF+y+yoq7Gk4k68z0lZoUQ0dNXZmbWqIoxHfK\r\n"
  "PbJQSDzdn+6eE/pQHl0OLKIRnaqyhcteqj/+ixluvXQOOtlUpFizf3pVEUY6x5LZ\r\n"
  "DRDqd/4jI0Uwk/TCpt0HZUH8JsUD7KW1m46WOqVkR84d5143/kqjgcBLtC/zhpkK\r\n"
  "VzCPS9tj7r6fEzwBZArP+4by+DFX4rYzjBaeDeZ7p0r631vwcfY2NxECk7/UlmU0\r\n"
  "c89N4kHjwwtt6X3d98ZDTp+iI3rMSaOxGUZ8wGF5CJnRVeC4+sGLmTxaVdeV2aZR\r\n"
  "YICEIk+3p2uiGjvfp9i5wA6iBPASsyu3UAiTgKviednCuUgZy3G/36zCCesWbxKh\r\n"
  "ryVA6qIrAmgq/YPIeQ6xZNyVZIMV\r\n"
  "-----END CERTIFICATE-----\r\n";

/** Client Private Key content */
static const char client_key[] =
  "-----BEGIN PRIVATE KEY-----\r\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCV+g342mQI2gCa\r\n"
  "rCfoKYk5HI6ZLNsScUrmqUvMnMBYph+AdC8GhbdfkkvikFUlM9G3Qm8gnGdTvsji\r\n"
  "u1BywFw3kkrqT8vrPvU/8iRzhQNEZE+QtxHAkp/6axTEpEaEhg3cITnl95Q4IHcs\r\n"
  "X9rjbqlcR4EkQtvEQDpqfmnF/d6Y0zbZW/PcAnbdu/apG7xFzt9DvWVlKDCLBvNr\r\n"
  "H6qyEkwneC7RgKzGaX7mxmoUeCFE99XRJcUMFraMCaD+ZE/d0Gba5SG2eJN+0Z18\r\n"
  "b1XiAfY3mE8VdZljivNjaCOV3Y5Mw0cEqFnZggU1qbhVdEbOtaoDWwF/YacdQz7Y\r\n"
  "j6dAuh9dAgMBAAECggEAPP4LBZvnV9Q0r7J4rkmKFXBgK7oaw8bQQ7sw6N8MuGCi\r\n"
  "6g4V+8yQlSz9cH/rKKyIysMZR4Vj3iJ2NwMfhfNl7XGwxta54wthGObkXRiIih1T\r\n"
  "YFKbRRo8Nk6rDQeT6BxOcaoPjk8f9614WdMHxTuBY9Zuli0cjBTkzN9pK8yBZNvN\r\n"
  "JXHxgDb/GTQ4ZWEZhh0fBNwXMF9oMbEn59K7Hh6XzsM85EV4M60LvvSiD/m94Oys\r\n"
  "3C6dFL7UaDJ2rmThLlItrCRzE+/2FgbjCAmEv2X2NXDzNEenZoCF9X1ie3v6caIT\r\n"
  "KdNyIZs5Vsaa4eWQdZYDthQUZjbNuK73CgxjqdJ0bwKBgQDSop6hjamHxUXpsaRv\r\n"
  "wHlDz7TXbYx/eyeCLtQESnKedYoSSWPw6tArMlLfQkFiuFh5lXIis08CvRYDf4Ns\r\n"
  "G/eglXO/iIF2lMm+bLEn5MGA+EwpuLHw7fvTpP6udLmElFa6hbmUyj/XgsZlIPA9\r\n"
  "Bl3UiLQi0s1/pNcb0ubZCdgPvwKBgQC2Rwdn+7W9gd1N9qPX79WagxuJ4DRUm21C\r\n"
  "vz2Ve2YMb7UR7+p5sktrtUEolXuPvfahJ+9FDIpaZigJRp2ySzcrMGHufLeW4UB/\r\n"
  "ZnPVWqtEBaQrpshWu5JHlycQAhjzFveEc9expHN/Fm5WkSDxma1NU2016hot+lz+\r\n"
  "Ajsm0cYX4wKBgQCwehmIZ8V7gLhDxVdtXgj73MG6oQlPIeMHOq7ebXW89+PX0G+Q\r\n"
  "wVvqZT5z2fIogSV3sNOw6SSwubYA9kwpPwFpJO6WsgsuTBj/l9eSAiJyKRa++gT0\r\n"
  "RKByQdI0Xo203AgSPMoxNIbqzKHmxwMhTf09fc/XQWF1qamkoT5S5+GDxwKBgDsz\r\n"
  "2r31TVQd5+k4oIK0TSaASuN/RL/uM5CoWLJCgCSt65vF1txsAn8bQeySkK1hP8ec\r\n"
  "FuTQa+dsorhQjUupjmOitUwmieKhirdWaWz0pAfV5TqgUxWImrxR5cgXRk8+OGp2\r\n"
  "zanPBgxTFsdbH94Y0eb5n9ERFiu005tU0i2LmNGNAoGARw3oORmknMdOOuDTtOl8\r\n"
  "yiIMzm6lmSIHI0hpJPPrVz1zzJY6CWwf9rvcGy3Rp+RpoYYgpgMFpfUeeH5zB7L7\r\n"
  "2d+Irfu5wuoWp5ImmAveFIHAdR9rqbzjz3M+/EEg/OKRUKSGHnfdExkhhZFKpJ+B\r\n"
  "ItceYk1hkNpYvnACWDpoIAc=\r\n"
  "-----END PRIVATE KEY-----\r\n";
#endif /* LFS_ENABLE */

/* Private function prototypes -----------------------------------------------*/

static void Subscription_process_task(void *arg);
static void publish_callback(void **state, struct mqtt_response_publish *published);
static void client_refresher(void *arg);
static void dns_lookup_callback(const char *name, const ip_addr_t *ipaddr, void *arg);
static int32_t tcp_client_connect(ip4_addr_t *ip, int32_t port);
static void APP_MQTT_StopAuxTasks(APP_Context_t *ctx);

/* Private function definitions ----------------------------------------------*/

/**
  * @brief  Stop and delete MQTT auxiliary tasks.
  * @param  ctx Pointer to the shared application context.
  * @retval None
  * @note   This helper deletes the subscription-processing task and the MQTT
  *         refresher task if they are currently running, then clears their
  *         task handles in the application context.
  */
static void APP_MQTT_StopAuxTasks(APP_Context_t *ctx)
{
  if (ctx == NULL)
  {
    return;
  }

  if (ctx->sub_task_handle != NULL)
  {
    vTaskDelete(ctx->sub_task_handle);
    ctx->sub_task_handle = NULL;
  }

  if (ctx->mqtt_refresher_task_handle != NULL)
  {
    vTaskDelete(ctx->mqtt_refresher_task_handle);
    ctx->mqtt_refresher_task_handle = NULL;
  }
}

/**
  * @brief  Process MQTT subscription messages received from the broker.
  * @param  arg Pointer to the shared application context.
  * @retval None
  * @note   This task waits on the subscription queue, parses each incoming JSON
  *         payload, logs supported fields, and applies simple commands such as
  *         LED control and system reboot. Topic and payload buffers are freed
  *         after processing.
  */
static void Subscription_process_task(void *arg)
{
  APP_Context_t *ctx = (APP_Context_t *)arg;
  BaseType_t ret;
  APP_MQTT_Data_t mqtt_data = {0};
  cJSON *json = NULL;
  cJSON *root = NULL;
  cJSON *child = NULL;
  cJSON_Hooks hooks =
  {
    .malloc_fn = pvPortMalloc,
    .free_fn = vPortFree,
  };

  (void)arg;
  cJSON_InitHooks(&hooks);

  if (ctx == NULL)
  {
    vTaskDelete(NULL);
    return;
  }

  for (;;)
  {
    ret = xQueueReceive(ctx->sub_msg_queue, &mqtt_data, portMAX_DELAY);
    if (ret == pdPASS)
    {
      LogInfo("MQTT Subscription Received on topic %s\n", (char *)mqtt_data.topic);

      root = cJSON_Parse((const char *)mqtt_data.message);
      if (root == NULL)
      {
        LogError("Processing error of JSON message\n");
        goto _cleanup;
      }

      child = cJSON_GetObjectItemCaseSensitive(root, "state");
      if (child != NULL)
      {
        child = cJSON_GetObjectItemCaseSensitive(child, "reported");
      }
      else
      {
        child = root;
      }

      if (child == NULL)
      {
        LogError("Invalid JSON content\n");
        goto _cleanup;
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "time");
      if ((json != NULL) && cJSON_IsString(json))
      {
        LogInfo("  %s: %s\n", json->string, json->valuestring);
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "rssi");
      if ((json != NULL) && cJSON_IsNumber(json))
      {
        LogInfo("  %s: %" PRIi32 "\n", json->string, (int32_t)json->valueint);
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "mac");
      if ((json != NULL) && cJSON_IsString(json))
      {
        LogInfo("  %s: %s\n", json->string, json->valuestring);
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "LedOn");
      if (json != NULL)
      {
        if (cJSON_IsBool(json))
        {
          ctx->red_led_status = (cJSON_IsTrue(json) == true);
          HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, ctx->red_led_status ? GPIO_PIN_RESET : GPIO_PIN_SET);
          LogInfo("  %s: %" PRIu16 "\n", json->string, (uint16_t)ctx->red_led_status);
        }
        else
        {
          LogError("JSON parsing error of %s value.\n", json->string);
        }
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "Reboot");
      if (json != NULL)
      {
        if (cJSON_IsBool(json))
        {
          if (cJSON_IsTrue(json) == true)
          {
            LogInfo("  %s requested in 1s ...\n", json->string);
            vTaskDelay(pdMS_TO_TICKS(1000U));
            HAL_NVIC_SystemReset();
          }
        }
        else
        {
          LogError("JSON parsing error of Reboot value.\n");
        }
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "temperature");
      if ((json != NULL) && cJSON_IsNumber(json))
      {
        LogInfo("  %s: %.2f\n", json->string, json->valuedouble);
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "pressure");
      if ((json != NULL) && cJSON_IsNumber(json))
      {
        LogInfo("  %s: %.2f\n", json->string, json->valuedouble);
      }

      json = cJSON_GetObjectItemCaseSensitive(child, "humidity");
      if ((json != NULL) && cJSON_IsNumber(json))
      {
        LogInfo("  %s: %.2f\n", json->string, json->valuedouble);
      }

_cleanup:
      cJSON_Delete(root);
      root = NULL;

      vPortFree(mqtt_data.topic);
      vPortFree(mqtt_data.message);
      mqtt_data.topic = NULL;
      mqtt_data.message = NULL;
    }
  }
}

/**
  * @brief  MQTT publish callback invoked when a subscribed message is received.
  * @param  state Unused user parameter provided by the MQTT library.
  * @param  published Pointer to the received publish descriptor.
  * @retval None
  * @note   This callback copies the topic and payload into dynamically
  *         allocated buffers, then pushes them into the subscription queue for
  *         deferred processing by the dedicated subscription task.
  */
static void publish_callback(void **state, struct mqtt_response_publish *published)
{
  APP_MQTT_Data_t mqtt_data = {0};

  (void)state;

  if ((s_ctx == NULL) || (published == NULL))
  {
    return;
  }

  mqtt_data.topic_length = published->topic_name_size + 1U;
  mqtt_data.topic = pvPortMalloc(mqtt_data.topic_length);
  if (mqtt_data.topic == NULL)
  {
    LogError("MQTT topic allocation failed\n");
    return;
  }

  memcpy(mqtt_data.topic, published->topic_name, published->topic_name_size);
  mqtt_data.topic[published->topic_name_size] = '\0';

  mqtt_data.message_length = published->application_message_size + 1U;
  mqtt_data.message = pvPortMalloc(mqtt_data.message_length);
  if (mqtt_data.message == NULL)
  {
    vPortFree(mqtt_data.topic);
    LogError("MQTT message allocation failed\n");
    return;
  }

  memcpy(mqtt_data.message, published->application_message, published->application_message_size);
  mqtt_data.message[published->application_message_size] = '\0';

  if ((s_ctx->sub_msg_queue == NULL) || (xQueueSendToBack(s_ctx->sub_msg_queue, &mqtt_data, 0) != pdPASS))
  {
    LogError("Failed to queue MQTT subscription data\n");
    vPortFree(mqtt_data.topic);
    vPortFree(mqtt_data.message);
  }
}

/**
  * @brief  Periodically service the MQTT client state machine.
  * @param  arg Pointer to the shared application context.
  * @retval None
  * @note   This task calls $mqtt\_sync(\cdot)$ at regular intervals while
  *         protecting access to the MQTT client with a mutex. It is used to
  *         process keep-alive traffic and incoming broker data.
  */
static void client_refresher(void *arg)
{
  APP_Context_t *ctx = (APP_Context_t *)arg;

  if (ctx == NULL)
  {
    vTaskDelete(NULL);
    return;
  }

  while (true)
  {
    if ((ctx->mqtt_client_mutex != NULL) && (xSemaphoreTake(ctx->mqtt_client_mutex, pdMS_TO_TICKS(20U)) == pdPASS))
    {
      (void)mqtt_sync(&s_client);
      xSemaphoreGive(ctx->mqtt_client_mutex);
    }

    vTaskDelay(pdMS_TO_TICKS(250U));
  }
}

/**
  * @brief  Asynchronous DNS resolution callback.
  * @param  name Hostname originally requested.
  * @param  ipaddr Resolved IP address, or NULL on failure.
  * @param  arg Pointer to the DNS result structure used by the caller.
  * @retval None
  * @note   This callback stores the DNS resolution result and signals the
  *         waiting task through the DNS event group.
  */
static void dns_lookup_callback(const char *name, const ip_addr_t *ipaddr, void *arg)
{
  APP_DNS_res_t *dns_res = (APP_DNS_res_t *)arg;
  (void)name;

  if (dns_res != NULL)
  {
    if (ipaddr != NULL)
    {
      dns_res->res = 0;
      dns_res->ipaddr = *ipaddr;
    }
    else
    {
      dns_res->res = -1;
    }
  }

  if ((s_ctx != NULL) && (s_ctx->dns_event_flags != NULL))
  {
    (void)xEventGroupSetBits(s_ctx->dns_event_flags, EVENT_FLAG_DNS_DONE);
  }
}

/**
  * @brief  Open and connect a TCP client socket to the specified remote host.
  * @param  ip Pointer to the destination IPv4 address.
  * @param  port Destination TCP port.
  * @retval Connected socket descriptor on success, negative value on failure.
  * @note   The socket is configured with $SO\_REUSEADDR$ and switched to
  *         non-blocking mode after a successful connection.
  */
static int32_t tcp_client_connect(ip4_addr_t *ip, int32_t port)
{
  int32_t fd;
  int32_t res;
  struct sockaddr_in addr;
  int32_t on = 1;

  LogInfo("tcp client connect %s:%" PRIi32 "\r\n", ip4addr_ntoa(ip), port);

  fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    LogError("socket create failed\r\n");
    return -2;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
#if defined(LWIP_SOCKET_HAVE_SA_LEN) || defined(sin_len)
  addr.sin_len = sizeof(addr);
#endif
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = ip4_addr_get_u32(ip);

  res = lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (res != 0)
  {
    LogError("setsockopt failed, res:%" PRIi32 "\r\n", res);
  }

  res = lwip_connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  if (res < 0)
  {
    LogError("connect failed, res:%" PRIi32 "\r\n", res);
    (void)close(fd);
    fd = -1;
  }

  if (fd != -1)
  {
    int32_t iMode = 1;
    (void)ioctlsocket(fd, FIONBIO, &iMode);
  }

  return fd;
}

/* Public API definitions ----------------------------------------------------*/

/**
  * @brief  Start the MQTT application.
  * @param  ctx Pointer to the shared application context.
  * @retval None
  * @note   This function initializes the MQTT runtime resources, including the
  *         DNS event group and subscription queue, then creates the main MQTT
  *         task. If MQTT is already started, the function returns immediately.
  */
void APP_MQTT_Start(APP_Context_t *ctx)
{
  if (ctx == NULL)
  {
    return;
  }

  if (ctx->mqtt_started)
  {
    LogInfo("MQTT already started\n");
    return;
  }

  s_ctx = ctx;

  ctx->dns_event_flags = xEventGroupCreate();
  if (ctx->dns_event_flags == NULL)
  {
    LogError("Failed to create DNS event group\n");
    return;
  }

  ctx->sub_msg_queue = xQueueCreate(10, sizeof(APP_MQTT_Data_t));
  if (ctx->sub_msg_queue == NULL)
  {
    LogError("Failed to create MQTT subscription queue\n");
    vEventGroupDelete(ctx->dns_event_flags);
    ctx->dns_event_flags = NULL;
    return;
  }

  if (xTaskCreate(APP_MQTT_Task, "mqtt_main",
                  MQTT_TASK_STACK_SIZE >> 2U,
                  ctx, MQTT_THREAD_PRIO, &ctx->mqtt_task_handle) != pdPASS)
  {
    LogError("Failed to create MQTT task\n");
    vQueueDelete(ctx->sub_msg_queue);
    ctx->sub_msg_queue = NULL;
    vEventGroupDelete(ctx->dns_event_flags);
    ctx->dns_event_flags = NULL;
    return;
  }

  ctx->mqtt_started = true;
  LogInfo("MQTT task started\n");
}

/**
  * @brief  Main MQTT task handling connection, subscription, publishing and cleanup.
  * @param  arg Pointer to the shared application context.
  * @retval None
  * @note   This task performs the complete MQTT lifecycle:
  *         - initializes SNTP,
  *         - resolves the broker hostname,
  *         - opens the TCP or TLS connection,
  *         - initializes and connects the MQTT client,
  *         - creates helper tasks for client refresh and message processing,
  *         - subscribes to the control topic,
  *         - periodically publishes telemetry,
  *         - performs cleanup on disconnect or error.
  */
void APP_MQTT_Task(void *arg)
{
  APP_Context_t *ctx = (APP_Context_t *)arg;
  W6X_Status_t ret;
  enum MQTTErrors mqtt_ret;
  EventBits_t eventBits;
  ip_addr_t resolved_ip = {0};
  int32_t sockfd = -1;
  struct custom_socket_handle handle;
  uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
  uint8_t Mac[6] = {0};

  RTC_TimeTypeDef rtc_time = {0};
  RTC_DateTypeDef rtc_date = {0};

  const uint8_t *hostname = mqtt_config.HostName;
  int32_t port = mqtt_config.HostPort;

#if (MQTT_SECURITY_LEVEL != 0)
  ssl_config_ctx_t *ssl_param = NULL;
  ssl_conn_t *conn_param = NULL;
#endif

  (void)arg;

  if (ctx == NULL)
  {
    vTaskDelete(NULL);
    return;
  }

  if (mqtt_config.DisableCleanSession != 0U)
  {
    connect_flags = 0U;
  }

  if (sntp_init(SNTP_TIMEZONE) != 0)
  {
    LogWarn("SNTP init failed\n");
  }

  ret = W6X_WiFi_Station_GetMACAddress(Mac);
  if (ret != W6X_STATUS_OK)
  {
    LogError("Failed to get Wi-Fi MAC Address, %" PRIi32 "\n", ret);
    goto _exit;
  }

  {
    APP_DNS_res_t dns_res = { .res = -1 };

    int32_t err = dns_gethostbyname((const char *)hostname, &resolved_ip, dns_lookup_callback, &dns_res);
    if (err == ERR_OK)
    {
      LogInfo("DNS resolved immediately: %s -> %s\n", (const char *)hostname, ipaddr_ntoa(&resolved_ip));
    }
    else if (err == ERR_INPROGRESS)
    {
      eventBits = xEventGroupWaitBits(ctx->dns_event_flags,
                                      EVENT_FLAG_DNS_DONE,
                                      pdTRUE, pdFALSE,
                                      pdMS_TO_TICKS(DNS_RESOLVE_TIMEOUT_MS));
      if ((eventBits & EVENT_FLAG_DNS_DONE) == 0U)
      {
        LogError("DNS Lookup timed out\n");
        goto _exit;
      }
      if (dns_res.res != 0)
      {
        LogError("DNS Lookup resolution failed\n");
        goto _exit;
      }
      resolved_ip = dns_res.ipaddr;
    }
    else
    {
      LogError("Failed to resolve hostname: %s, err=%" PRIi32 "\n", (const char *)hostname, err);
      goto _exit;
    }
  }

  sockfd = tcp_client_connect(&(resolved_ip.u_addr.ip4), port);
  if (sockfd < 0)
  {
    LogError("Failed to open socket: %" PRIi32 "\n", sockfd);
    goto _exit;
  }

  handle.type = MQTTC_PAL_CONNTION_TYPE_TCP;
  handle.ctx.fd = sockfd;

#if (MQTT_SECURITY_LEVEL != 0)
  {
    char *sni = NULL;
    ssl_conn_param_t params = {0};

    if ((mqtt_config.Scheme == 2U) || (mqtt_config.Scheme == 4U))
    {
      params.ca_cert = (char *)ca_certificate;
      params.ca_cert_len = strlen(ca_certificate) + 1U;
    }
    if (mqtt_config.Scheme >= 3U)
    {
      params.own_cert = (char *)client_certificate;
      params.own_cert_len = strlen(client_certificate) + 1U;
      params.private_cert = (char *)client_key;
      params.private_cert_len = strlen(client_key) + 1U;
    }

    if (mqtt_config.SNI[0] != 0)
    {
      sni = (char *)mqtt_config.SNI;
    }

    ssl_param = ssl_configure(&params, 1);
    if (ssl_param == NULL)
    {
      LogError("ssl_configure failed\n");
      goto _close_sock;
    }

    conn_param = ssl_secure_connection(sockfd, ssl_param, 1, sni);
    if (conn_param == NULL)
    {
      LogError("ssl_secure_connection failed\n");
      goto _close_sock;
    }

    handle.type = MQTTC_PAL_CONNTION_TYPE_TLS;
    handle.ctx.ssl_ctx = &(conn_param->ssl);
  }
#endif

  mqtt_init(&s_client, &handle,
            ctx->mqtt_sendbuf, sizeof(ctx->mqtt_sendbuf),
            ctx->mqtt_recvbuf, sizeof(ctx->mqtt_recvbuf),
            publish_callback);

  {
    char *username = NULL;
    char *password = NULL;

    if (mqtt_config.Scheme >= 1U)
    {
      username = (char *)mqtt_config.MQUserName;
      password = (char *)mqtt_config.MQUserPwd;
    }

    if (xSemaphoreTake(ctx->mqtt_client_mutex, pdMS_TO_TICKS(500U)) == pdPASS)
    {
      mqtt_ret = mqtt_connect(&s_client,
                              (char const *)mqtt_config.MQClientId,
                              NULL, NULL, 0U,
                              username, password,
                              connect_flags, 400U);
      xSemaphoreGive(ctx->mqtt_client_mutex);
    }
    else
    {
      LogError("MQTT connect mutex timeout\n");
      goto _close_sock;
    }

    if ((mqtt_ret != MQTT_OK) || (s_client.error != MQTT_OK))
    {
      LogError("MQTT Connect failed: %s\n", mqtt_error_str(s_client.error));
      goto _close_sock;
    }

    LogInfo("MQTT Connect successful\n");
  }

  ctx->mqtt_connected = true;

  if (xTaskCreate(client_refresher, "mqtt_ref",
                  REFRESHER_TASK_STACK_SIZE >> 2U,
                  ctx, REFRESHER_THREAD_PRIO,
                  &ctx->mqtt_refresher_task_handle) != pdPASS)
  {
    LogError("Failed to create MQTT refresher task\n");
    goto _disconnect;
  }

  if (xTaskCreate(Subscription_process_task, "mqtt_sub",
                  SUBSCRIPTION_TASK_STACK_SIZE >> 2U,
                  ctx, SUBSCRIPTION_THREAD_PRIO,
                  &ctx->sub_task_handle) != pdPASS)
  {
    LogError("Failed to create MQTT subscription task\n");
    goto _disconnect;
  }

  (void)snprintf((char *)ctx->mqtt_topic, MQTT_TOPIC_BUFFER_SIZE, "/devices/%s/control", mqtt_config.MQClientId);
  LogInfo("Subscribing to topic %s.\n", ctx->mqtt_topic);

  if (xSemaphoreTake(ctx->mqtt_client_mutex, pdMS_TO_TICKS(500U)) == pdPASS)
  {
    (void)mqtt_subscribe(&s_client, (char *)ctx->mqtt_topic, 0);
    xSemaphoreGive(ctx->mqtt_client_mutex);
  }

  while (1)
  {
    uint32_t len = 0U;
    W6X_WiFi_StaStateType_e State = W6X_WIFI_STATE_STA_NO_STARTED_CONNECTION;
    W6X_WiFi_Connect_t ConnectData = {0};

    (void)HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
    (void)HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);
    (void)W6X_WiFi_Station_GetState(&State, &ConnectData);

    len = (uint32_t)snprintf((char *)ctx->mqtt_pubmsg, MQTT_MSG_BUFFER_SIZE,
                             "{\n"
                             " \"state\": {\n"
                             "  \"reported\": {\n"
                             "   \"time\": \"%02" PRIu16 "-%02" PRIu16 "-%02" PRIu16 " %02" PRIu16 ":%02" PRIu16 ":%02" PRIu16 "\",\n"
                             "   \"mac\": \"" MACSTR "\",\n"
                             "   \"rssi\": %" PRIi32,
                             rtc_date.Year, rtc_date.Month, rtc_date.Date,
                             rtc_time.Hours, rtc_time.Minutes, rtc_time.Seconds,
                             MAC2STR(Mac), ConnectData.Rssi);

    len += (uint32_t)snprintf((char *)&ctx->mqtt_pubmsg[len], MQTT_MSG_BUFFER_SIZE - len, "\n  }\n }\n}");

    if (len >= MQTT_MSG_BUFFER_SIZE)
    {
      len = MQTT_MSG_BUFFER_SIZE - 1U;
      ctx->mqtt_pubmsg[len] = 0U;
    }

    (void)snprintf((char *)ctx->mqtt_topic, MQTT_TOPIC_BUFFER_SIZE, "/sensors/%s", mqtt_config.MQClientId);

    {
      bool publish_attempted = false;
      mqtt_ret = MQTT_OK;

      if ((xEventGroupGetBits(ctx->app_runtime_flags) & EVENT_FLAG_STREAM_TX_BUSY) != 0U)
      {
        LogWarn("Skipping MQTT publish during RTP TX busy window\n");
      }
      else
      {
        TickType_t now = xTaskGetTickCount();
        TickType_t postpone_until = ctx->mqtt_publish_postpone_until;

        if (postpone_until > now)
        {
          TickType_t delay_ticks = postpone_until - now;
          LogDebug("Postponing MQTT publish by %" PRIu32 " ms due to recent large RTP frame\n", APP_TICKS_TO_MS(delay_ticks));
          vTaskDelay(delay_ticks);
        }

        if (xSemaphoreTake(ctx->mqtt_client_mutex, pdMS_TO_TICKS(100U)) == pdPASS)
        {
          publish_attempted = true;
          mqtt_ret = mqtt_publish(&s_client,
                                  (char *)ctx->mqtt_topic,
                                  (char *)ctx->mqtt_pubmsg,
                                  len,
                                  MQTT_PUBLISH_QOS_0);
          xSemaphoreGive(ctx->mqtt_client_mutex);
        }
        else
        {
          LogWarn("Skipping MQTT publish: client mutex busy\n");
        }
      }

      if (publish_attempted)
      {
        if (mqtt_ret == MQTT_OK)
        {
          LogInfo("MQTT Publish OK\n");
        }
        else
        {
          LogError("MQTT publish failed: %s\n", mqtt_error_str(mqtt_ret));
          break;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(5000U));
  }

_disconnect:
  ctx->mqtt_connected = false;

  APP_MQTT_StopAuxTasks(ctx);

  if ((ctx->mqtt_client_mutex != NULL) &&
      (xSemaphoreTake(ctx->mqtt_client_mutex, pdMS_TO_TICKS(500U)) == pdPASS))
  {
    (void)mqtt_disconnect(&s_client);
    xSemaphoreGive(ctx->mqtt_client_mutex);
  }

_close_sock:
  if (sockfd >= 0)
  {
    close(sockfd);
  }

_exit:
  ctx->mqtt_started = false;

  APP_MQTT_StopAuxTasks(ctx);

  if (ctx->sub_msg_queue != NULL)
  {
    vQueueDelete(ctx->sub_msg_queue);
    ctx->sub_msg_queue = NULL;
  }

  if (ctx->dns_event_flags != NULL)
  {
    vEventGroupDelete(ctx->dns_event_flags);
    ctx->dns_event_flags = NULL;
  }

  LogInfo("MQTT task stopped\n");
  ctx->mqtt_task_handle = NULL;
  s_ctx = NULL;
  vTaskDelete(NULL);
}
