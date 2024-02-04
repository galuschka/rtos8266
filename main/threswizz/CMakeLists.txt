idf_component_register( SRCS threswizz_main.cpp AnalogReader.cpp Control.cpp Monitor.cpp Input.cpp
                INCLUDE_DIRS ""
           PRIV_INCLUDE_DIRS ../common ../compat ../../esp-open-rtos/extras
                    REQUIRES common
)
