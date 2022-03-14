idf_component_register(SRCS Indicator.cpp
                            Wifi.cpp
                            WebServer.cpp
                            HttpHelper.cpp
                            HttpParser.cpp
                            Updator.cpp
                            Mqtinator.cpp
               INCLUDE_DIRS ""
                   REQUIRES esp_common
                            pthread
                            nvs_flash
                            app_update
                            esp_http_server
                            esp_http_client
                            mqtt
)
