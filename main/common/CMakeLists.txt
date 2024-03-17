idf_component_register(SRCS Init.cpp
                            BootCnt.cpp
                            Indicator.cpp
                            Wifi.cpp
                            WebServer.cpp
                            HttpHelper.cpp
                            HttpParser.cpp
                            Updator.cpp
                            Mqtinator.cpp
                            Temperator.cpp
                            Relay.cpp
                            Fader.cpp
                            Json.cpp
               INCLUDE_DIRS ""
          PRIV_INCLUDE_DIRS ../common ../compat ../../esp-open-rtos/extras
                   REQUIRES esp_common
                            pthread
                            nvs_flash
                            app_update
                            esp_http_server
                            esp_http_client
                            mqtt
)
