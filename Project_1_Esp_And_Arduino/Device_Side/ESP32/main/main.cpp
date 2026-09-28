#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "cJSON.h"

// Configuration
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASS           "YOUR_WIFI_PASSWORD"
#define FASTAPI_URL         "http://192.168.1.100:8000/api/sensor-data" // Update with your PC/Server IP

#define UART_PORT_NUM       UART_NUM_1
#define UART_TX_PIN         GPIO_NUM_17
#define UART_RX_PIN         GPIO_NUM_16
#define BUF_SIZE            1024

static const char *TAG = "ESP32_GATEWAY";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// Wi-Fi Event Handler
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying Wi-Fi connection...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Connected to Wi-Fi");
    }
}

// Initialize Wi-Fi Station
void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    wifi_config_t wifi_config = {};

    strncpy(
        (char *)wifi_config.sta.ssid,
        WIFI_SSID,
        sizeof(wifi_config.sta.ssid)
    );

    strncpy(
        (char *)wifi_config.sta.password,
        WIFI_PASS,
        sizeof(wifi_config.sta.password)
    );

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait until Wi-Fi connection is established
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

// Initialize SNTP to get real-time Date & Time
void sntp_init_time(void) {
    ESP_LOGI(TAG, "Initializing SNTP time sync...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    // Wait for time synchronization
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < 10) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/10)", retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

// Initialize UART for Arduino communication
void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
}

// Send HTTP POST payload to FastAPI
void send_http_post(const char *json_data) {
    esp_http_client_config_t config = {
        .url = FASTAPI_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_data, strlen(json_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d", esp_http_client_get_status_code(client));
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// Processing task
void uart_rx_task(void *pvParameters) {
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    if (data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate UART buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        // Read data line from UART
        int len = uart_read_bytes(UART_PORT_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            data[len] = '\0';
            
            // Expected CSV string from Arduino: "24.50,620,812\r\n"
            float heat = 0.0;
            int moisture = 0, light = 0;

            if (sscanf((char *)data, "%f,%d,%d", &heat, &moisture, &light) == 3) {
                // Fetch current time
                time_t now;
                struct tm timeinfo;
                time(&now);
                localtime_r(&now, &timeinfo);

                char date_buf[16];
                char time_buf[16];
                strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &timeinfo);
                strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &timeinfo);

                // Build cJSON payload
                cJSON *root = cJSON_CreateObject();
                cJSON_AddStringToObject(root, "date", date_buf);
                cJSON_AddStringToObject(root, "time", time_buf);
                cJSON_AddNumberToObject(root, "heat", heat);
                cJSON_AddNumberToObject(root, "moisture", moisture);
                cJSON_AddNumberToObject(root, "light", light);

                char *json_string = cJSON_PrintUnformatted(root);
                ESP_LOGI(TAG, "Sending Payload: %s", json_string);

                // Post to FastAPI endpoint
                send_http_post(json_string);

                // Cleanup cJSON memory
                cJSON_free(json_string);
                cJSON_Delete(root);
            }
        }
    }
    free(data);
}

extern "C" void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    // Initialize systems
    wifi_init_sta();
    sntp_init_time();
    uart_init();

    // Launch task
    xTaskCreate(
        uart_rx_task,
        "uart_rx_task",
        4096,
        NULL,
        10,
        NULL
    );
}
