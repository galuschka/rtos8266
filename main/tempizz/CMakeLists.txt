if(0)

idf_component_register( SRCS tempizz_main.cpp Tempizz.cpp
                             ../../esp-open-rtos/extras/onewire/onewire.c
                             ../../esp-open-rtos/extras/ds18b20/ds18b20.c
                INCLUDE_DIRS ""
           PRIV_INCLUDE_DIRS ../common ../compat ../../esp-open-rtos/extras
                    REQUIRES common
)

endif()
