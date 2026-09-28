idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        freertos
        esp_system
        esp_wifi
        esp_event
        esp_netif
        log
        nvs_flash
        lwip
        esp_http_client
        esp_driver_uart
        esp_driver_gpio
        json
)
