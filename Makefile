#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := keypad
PROJECT_VER := $(shell git describe --abbr=5 --dirty=+)

IDF_PATH := sdk

include $(IDF_PATH)/make/project.mk
