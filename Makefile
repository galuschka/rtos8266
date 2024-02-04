#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := $(PNAME)
PROJECT_VER := $(shell git describe --abbr=5 --dirty=+)

IDF_PATH := sdk

# COMPONENT_DIRS: Defaults to $(IDF_PATH)/components $(PROJECT_PATH)/components $(PROJECT_PATH)/main EXTRA_COMPONENT_DIRS
COMPONENT_DIRS := $(abspath main/$(PROJECT_NAME)) \
                  $(abspath main/common) \
                  $(EXTRA_COMPONENT_DIRS) \
                  $(abspath $(IDF_PATH)/components)

include $(IDF_PATH)/make/project.mk
