IF(0)

idf_component_register( SRCS keypad_main.cpp Keypad.cpp
                INCLUDE_DIRS ""
                PRIV_INCLUDE_DIRS ../common
                    REQUIRES common
)

ENDIF()
