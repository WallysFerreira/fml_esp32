#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_websocket_client.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "json_parser.h"

#define SSID "Arctic Monkeys"
#define pass "ityttmom0209"
//#define SERVER "wss://192.168.0.102:8080/echo"
//#define SERVER "wss://demo.piesocket.com/v3/channel_123?api_key=VCXCEuvhGcBDP7XhiJJUDvR1e1D3eiVjgZ9VRiaV&notify_self"
#define SERVER "wss://m9sli48ocd.execute-api.us-east-2.amazonaws.com/test/?type=board&ID=ESP32C6&name=PlacaFML"

esp_websocket_client_handle_t client;
static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;
static EventGroupHandle_t wifi_event_group;
static int s_retry_num = 0;
static led_strip_handle_t led_strip;
int red_value = 0;
int green_value = 0;
int blue_value = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 2) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(wifi_event_group, BIT1);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, BIT0);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
      printf("WEBSOCKET_EVENT_CONNECTED\n");
      //esp_websocket_client_send_text(client, "{\"ID\": 1, \"Name\": \"Arnott\"}", 27, portMAX_DELAY);
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      printf("WEBSOCKET_EVENT_DISCONNECTED\n");
      break;
    case WEBSOCKET_EVENT_DATA:
      //ESP_LOGW(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
      if (data->data_len > 0) {
        printf("Received: %.*s\n", data->data_len, (char *)data->data_ptr);

        jparse_ctx_t jctx;
        char attribute[10];
        char controllerID[64];
        char answer[190];
        int len = 0;
        int array_len;

        json_parse_start(&jctx, data->data_ptr, data->data_len);
        json_obj_get_string(&jctx, "attribute", attribute, sizeof(attribute));
        json_obj_get_string(&jctx, "requestedBy", controllerID, sizeof(controllerID));

        if (strcmp(attribute, "red") == 0) {
          json_obj_get_int(&jctx, "value", &red_value);
          len = sprintf(answer, "{\"action\":\"answerchangerequest\",\"data\":{\"controllerID\":\"%s\",\"confirmed\":true,\"attribute\":\"%s\",\"value\":%d}}", controllerID, attribute, red_value);
        } else if (strcmp(attribute, "green") == 0) {
          json_obj_get_int(&jctx, "value", &green_value);
          len = sprintf(answer, "{\"action\":\"answerchangerequest\",\"data\":{\"controllerID\":\"%s\",\"confirmed\":true,\"attribute\":\"%s\",\"value\":%d}}", controllerID, attribute, green_value);
        } else if (strcmp(attribute, "blue") == 0) {
          json_obj_get_int(&jctx, "value", &blue_value);
          len = sprintf(answer, "{\"action\":\"answerchangerequest\",\"data\":{\"controllerID\":\"%s\",\"confirmed\":true,\"attribute\":\"%s\",\"value\":%d}}", controllerID, attribute, blue_value);
        } else if (strcmp(attribute, "rgb") == 0) {
          json_obj_get_array(&jctx, "value", &array_len);
          
          if (array_len != 3) {
            len = sprintf(answer, "{\"action\":\"answerchangerequest\",\"data\":{\"controllerID\":\"%s\",\"confirmed\":false,\"reason\":\"was expecting array of length 3\"}}", controllerID);
          } else {
            json_arr_get_int(&jctx, 0, &red_value);
            json_arr_get_int(&jctx, 1, &green_value);
            json_arr_get_int(&jctx, 2, &blue_value);
          }

          json_obj_leave_array(&jctx);
        }

        printf("Red: %d\nGreen: %d\nBlue: %d\n", red_value, green_value, blue_value);
        led_strip_set_pixel(led_strip, 0, red_value, green_value, blue_value);
        led_strip_refresh(led_strip);

        esp_websocket_client_send_text(client, answer, len, portMAX_DELAY);
      }

      xTimerReset(shutdown_signal_timer, portMAX_DELAY);
      break;
    case WEBSOCKET_EVENT_ERROR:
      printf("WEBSOCKET_EVENT_ERROR\n");
      break;
    }
}

static void shutdown_signaler(TimerHandle_t xTimer)
{
    xSemaphoreGive(shutdown_sema);
}

void app_main(void)
{
  esp_err_t ret = nvs_flash_init(); 
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { 
    ESP_ERROR_CHECK(nvs_flash_erase()); 
    ret = nvs_flash_init(); 
  } 
  ESP_ERROR_CHECK(ret); 

  ESP_ERROR_CHECK(esp_netif_init());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  printf("Initializing WiFI... ");

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

  wifi_config_t wifi_config = {
    .sta = {
      .ssid = SSID,
      .password = pass,
      .threshold.authmode = WIFI_AUTH_WPA2_PSK,
      .pmf_cfg = {
        .capable = true,
        .required = false,
      },
    },
  };

  printf("Trying to connect to %s\n", SSID);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  wifi_event_group = xEventGroupCreate();
  EventBits_t bits = xEventGroupWaitBits(
    wifi_event_group,
    BIT0 | BIT1,
    pdFALSE,
    pdFALSE,
    portMAX_DELAY);

  if (bits & BIT0) {
    printf("Connected!\n");
  } else if (bits & BIT1) {
    printf("Failed!\n");
    return;
  } else {
    printf("Unexpected error!\n");
  }

  esp_websocket_client_config_t websocket_cfg = {};
  websocket_cfg.uri = SERVER;

  shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", 10 * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
  shutdown_sema = xSemaphoreCreateBinary();
  client = esp_websocket_client_init(&websocket_cfg);
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);
  esp_websocket_client_start(client);
  xTimerStart(shutdown_signal_timer, portMAX_DELAY);
  /*
  char data[6];
  while (1) {
    if (esp_websocket_client_is_connected(client)) {
      for (int i = 0; i < 3; i++) {
        int len = sprintf(data, "Oi %d", i);
        printf("Sending %s\n", data);
        esp_websocket_client_send_text(client, data, len, portMAX_DELAY);
      }

      break;
    }
  }
  */
  led_strip_config_t strip_config = {
    .strip_gpio_num = GPIO_NUM_8,
    .max_leds = 1, // at least one LED on board
  };
  led_strip_rmt_config_t rmt_config = {
    .resolution_hz = 10 * 1000 * 1000, // 10MHz
  };
  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);

  while (1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  xSemaphoreTake(shutdown_sema, portMAX_DELAY);
  esp_websocket_client_close(client, 2000 / portTICK_PERIOD_MS);
  esp_websocket_client_destroy(client);
  vEventGroupDelete(wifi_event_group);
}

