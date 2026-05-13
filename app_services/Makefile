TOP_CONFIG_FILE ?=
#
include $(TOP_CONFIG_FILE)
#
include $(SRCTREE)/Kbuild
CVI_MPI_DIR := $(CONFIG_CVI_PLATFORM_DIR)/cvi_mpi
#

SDIR		:= $(CURDIR)/src
SRCS_C		:=
SRCS_CPP	:=
ifeq ($(CONFIG_SERVICES_ADAS),y)
SRCS_C		+= $(SDIR)/adas/adas_service.c
endif
ifeq ($(CONFIG_SERVICES_AUDIO),y)
SRCS_C		+= $(SDIR)/audio/acap.c
endif
ifeq ($(CONFIG_SERVICES_IMAGE_VIEWER),y)
SRCS_CPP	+= $(SDIR)/image_viewer/asimage_viewer.cpp
SRCS_CPP	+= $(SDIR)/image_viewer/image_viewer.cpp
endif
ifeq ($(CONFIG_SERVICES_LIVEVIEW),y)
SRCS_C      += $(SDIR)/liveview/liveview.c
endif
ifeq ($(CONFIG_SERVICES_PHOTO),y)
SRCS_C      += $(SDIR)/photo/photo_service.c
endif
ifeq ($(CONFIG_SERVICES_PLAYER),y)
SRCS_C      += $(SDIR)/player/ps_event.c
SRCS_C      += $(SDIR)/player/ps_main.c
SRCS_C      += $(SDIR)/player/ps_param.c
endif
ifeq ($(CONFIG_SERVICES_QRCODE),y)
SRCS_C      += $(SDIR)/qrcode/qrcode.c
SRCS_C      += $(SDIR)/qrcode/qrcode_ser.c
ifeq ($(CONFIG_ZBAR), y)
ZBAR_DIR    ?= $(OSS_DIR)/zbar-0.10
INCS        += -I$(ZBAR_DIR)/include
INCS        += -I$(ZBAR_DIR)/zbar
CFLAGS      += -DENABLE_ZBAR
endif
endif
ifeq ($(CONFIG_SERVICES_RECORD),y)
SRCS_C      += $(SDIR)/recorder/rs_event.c
SRCS_C      += $(SDIR)/recorder/rs_interface.c
SRCS_C      += $(SDIR)/recorder/rs_master.c
SRCS_C      += $(SDIR)/recorder/rs_param.c
endif
ifeq ($(CONFIG_SERVICES_SUBVIDEO),y)
SRCS_C      += $(SDIR)/subvideo/subvideo_service.c
endif

ifeq ($(CONFIG_SERVICES_RTSP),y)
SRCS_C      += $(SDIR)/rtsp/rtspservice.c
endif
ifeq ($(CONFIG_SERVICES_SPEECH),y)
SRCS_C      += $(SDIR)/speech/speech.c
endif
ifeq ($(CONFIG_SERVICES_STORAGE),y)
SRCS_C      += $(SDIR)/storage/storage.c
endif
#
ifeq ($(CONFIG_SERVICES_MD),y)
SRCS_C      += $(SDIR)/md/md_ser.c
endif
ifeq ($(CONFIG_SERVICES_FACEP),y)
SRCS_C      += $(SDIR)/facep/facep_service.c
endif
ifeq ($(CONFIG_SERVICES_ANIP),y)
SRCS_C      += $(SDIR)/anip/anip_service.c
endif

SRCS_C      += $(SDIR)/jpegp/jpegp.c
SRCS_C      += $(SDIR)/zoomp/zoomp.c

CVI_MPI_DIR				?= $(SRCTREE)/components/cvi_mpi
#
INCS		+= -I$(COMMON_DIR)
INCS		+= -I$(SDIR)
INCS		+= -I$(CURDIR)/include
INCS		+= -I$(RINGBUFFER_DIR)/include
INCS		+= -I$(OSAL_DIR)/include
INCS		+= -I$(CVI_MPI_DIR)/include
INCS		+= -I$(CVI_MPI_DIR)/include/isp
INCS		+= -I$(SYSUTILS_DIR)/include
INCS		+= -I$(MAPI_DIR)/include
INCS		+= -I$(DTCF_DIR)/include
INCS		+= -I$(PLAYER_DIR)/include
INCS		+= -I$(DEMUXER_DIR)/include
INCS		+= -I$(FILE_RECOVER_DIR)/include
INCS		+= -I$(PERF_DIR)/include
INCS		+= -I$(RECORDER_DIR)/include
INCS		+= -I$(MUXER_DIR)/include
INCS 		+= -I$(EXIF_DIR)/include
INCS		+= -I$(STORAGE_DIR)/include
INCS		+= -I$(THUBNAIL_EXTRACTOR_DIR)/include
INCS		+= -I$(RTSP_DIR)/include
INCS		+= -I$(HAL_DIR)/include
INCS		+= -I$(TDL_SDK_DIR)/include
INCS		+= -I$(FFMPEG_DIR)/include
INCS		+= -I$(KT_ANI_SDK_DIR)/include
INCS		+= -I$(SRCTREE)/applications/dashcam/ui/aicamera/dc309/include

#
LIB_DIR		?= lib
TARGET_DIR	:= $(CURDIR)/$(LIB_DIR)
TARGET_A	:= $(TARGET_DIR)/libapp_service.a
TARGET_SO	:= $(TARGET_DIR)/libapp_service.so
# need check, wangliang.wang 2024/8/15.
CXX_STD		:= -std=gnu++14
CXXFLAGS	+= -Werror=all -fsigned-char -fdiagnostics-color=always -Wno-stringop-truncation
CXXFLAGS	+= -Wno-unused-result -Wno-unused-parameter -Wno-unused-variable -Wno-ignored-qualifiers
#

# Config file path
PARAM_PATH_PREFIX = $(SRCTREE)/applications/$(CFG_PDT)/param/inicfgs

ifeq ($(CONFIG_APP_DASHCAM), y)
PARAM_FULL_PATH=$(PARAM_PATH_PREFIX)/$(INI_PDT)
MAX_SENSOR_NUM = $(shell grep "cam_num" $(PARAM_FULL_PATH)/config_devmng.ini | awk '{print $$3}')
KBUILD_DEFINES += -DMAX_CAMERA_INSTANCES=$(MAX_SENSOR_NUM)
endif
CFLAGS += $(KBUILD_DEFINES)
CXXFLAGS += $(KBUILD_DEFINES)
include $(COMMON_DIR)/common_lib.mk

clean:
	$(call print_target)
	$(Q)rm -rf $(TARGET_DIR)
	$(Q)rm -rf $(ODIR)
