COMPONENT_OBJS    := threswizz_main.o AnalogReader.o Control.o Monitor.o Input.o \
                     ../../esp-open-rtos/extras/onewire/onewire.o \
                     ../../esp-open-rtos/extras/ds18b20/ds18b20.o
COMPONENT_SRCDIRS := . ../../esp-open-rtos/extras/onewire \
                       ../../esp-open-rtos/extras/ds18b20
COMPONENT_PRIV_INCLUDEDIRS := ../common ../compat ../../esp-open-rtos/extras ../../esp-open-rtos/include
