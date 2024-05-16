idf_component_register(SRCS "hello_world_main.c"
                    INCLUDE_DIRS ""
                    REQUIRES esp_websocket_client
                    REQUIRES nvs_flash
                    REQUIRES driver
                    REQUIRES led_strip
                    REQUIRES json_parser
                    REQUIRES esp_wifi)
