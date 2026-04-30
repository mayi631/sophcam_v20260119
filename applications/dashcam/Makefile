TOP_CONFIG_FILE ?=
#
include $(TOP_CONFIG_FILE)
#
include $(SRCTREE)/Kbuild
CVI_MPI_DIR := $(CONFIG_CVI_PLATFORM_DIR)/cvi_mpi

TOPTARGETS := all clean
.PHONY: $(TOPTARGETS)

MEDIA_DIR				:= $(SRCTREE)/applications/dashcam/media
PARAM_DIR				:= $(SRCTREE)/applications/dashcam/param/core
PARAMINI2BIN_DIR		:= $(SRCTREE)/applications/dashcam/param/ini2bin
NET_DIR					:= $(SRCTREE)/applications/dashcam/netctrl
USBCTRL_DIR				:= $(SRCTREE)/applications/dashcam/usbctrl
UI_DIR					:= $(SRCTREE)/applications/dashcam/ui/$(CFG_PDT_SUB)/$(UI_PACKET)
MANAGEER_DIR			:= $(SRCTREE)/applications/manager
MODE_DIR				:= $(SRCTREE)/applications/dashcam/mode
APP_SERVICE_DIR			:= $(SRCTREE)/app_services
CVI_MPI_DIR				?= $(SRCTREE)/components/cvi_mpi

# include
INCS			+= -I$(COMMON_DIR)
INCS			+= -I$(SRCTREE)/applications/include
INCS			+= -I$(CVI_MPI_DIR)/include
INCS			+= -I$(CVI_MPI_DIR)/include/isp
INCS			+= -I$(OSAL_DIR)/include
INCS			+= -I$(SYSUTILS_DIR)/include
INCS			+= -I$(INI_DIR)/include
INCS			+= -I$(MAPI_DIR)/include
INCS			+= -I$(APP_SERVICE_DIR)/include
INCS			+= -I$(DTCF_DIR)/include
INCS			+= -I$(STORAGE_DIR)/include
INCS			+= -I$(HAL_DIR)/include
INCS			+= -I$(RECORDER_DIR)/include
INCS			+= -I$(MUXER_DIR)/include
INCS			+= -I$(PARAM_DIR)/include
INCS			+= -I$(MEDIA_DIR)/include
INCS			+= -I$(COMMON_DIR)/
ifeq ($(CONFIG_OSS_AWTKCODE), y)
INCS			+= -I$(AWTK_DIR) -I$(AWTK_DIR)/awtk/src \
				-I$(AWTK_DIR)/awtk/src/ext_widgets -I$(AWTK_DIR)/awtk/src/base \
				-I$(AWTK_DIR)/awtk-linux-fb/awtk-port -DLOAD_ASSET_WITH_MMAP \
				-I$(AWTK_DIR)/awtk/3rd/cjson
endif
INCS            += -I$(CSI2D_DIR)/include
INCS			+= -I$(UI_DIR)/include
INCS			+= -I$(UI_DIR)/src/guiguider_ui
INCS			+= -I$(MANAGEER_DIR)/include
INCS			+= -I$(MODE_DIR)/include
INCS			+= -I$(HAL_DIR)/src/screen/include
INCS			+= -I$(THTTPD_DIR)/include
INCS			+= -I$(NET_DIR)/include
INCS			+= -I$(USBCTRL_DIR)/include
INCS			+= -I$(UPGRADE_DIR)/include
INCS			+= -I$(PLAYER_DIR)/include
INCS			+= -I$(DEMUXER_DIR)/include
INCS			+= -I$(FILE_RECOVER_DIR)/include
INCS			+= -I$(PERF_DIR)/include
INCS			+= -I$(RINGBUFFER_DIR)/include
INCS			+= -I$(THUBNAIL_EXTRACTOR_DIR)/include
INCS			+= -I$(RTSP_DIR)/include
INCS			+= -I$(EXIF_DIR)/include
ifeq ($(CONFIG_OSS_FFMPEG), y)
INCS			+= -I$(FFMPEG_DIR)/include
endif
ifeq ($(CONFIG_OSS_LVGL), y)
INCS			+= -I$(LVGL_DIR)/
INCS			+= -I$(LVGL_DIR)/..
endif
INCS			+= -I$(UI_DIR)/third_party/freetype/freetype2
INCS			+= -I$(PARAMINI2BIN_DIR)/include
INCS			+= -I$(FLASH_TOOL_DIR)/include
INCS			+= -I$(UI_DIR)/third_party/wpa/include

#lib
DYN_LIBS		+= -L$(UI_DIR)/third_party/wpa/lib/$(PROCESSED_CONFIG_CROSS_COMPILE) -lwpa_client
ifeq ($(CONFIG_OSS_AWTKCODE), y)
LIBS			+= -L$(AWTK_DIR)/awtk-linux-fb/build/lib -lawtk
endif
ifeq ($(findstring riscv64, $(CONFIG_CROSS_COMPILE)), riscv64)
LIBS            += -L$(CSI2D_DIR)/lib -lcsi2d
endif
LIBS			+= -L$(MANAGEER_DIR)/lib -lapp_mng
LIBS			+= -L$(APP_SERVICE_DIR)/lib -lapp_service
LIBS			+= -L$(MAPI_DIR)/lib -lcomp_mapi
LIBS			+= -L$(HAL_DIR)/lib -lcomp_hal
LIBS			+= -L$(FLASH_TOOL_DIR)/lib -lcomp_flash
LIBS			+= -L$(PLAYER_DIR)/lib -lcomp_player
LIBS			+= -L$(FILE_RECOVER_DIR)/lib -lcomp_file_recover
LIBS			+= -L$(PERF_DIR)/lib -lcomp_perf
LIBS			+= -L$(RECORDER_DIR)/lib -lcomp_recorder
LIBS			+= -L$(MUXER_DIR)/lib -lcomp_muxer
LIBS			+= -L$(RINGBUFFER_DIR)/lib -lcomp_ringbuffer
LIBS			+= -L$(THUBNAIL_EXTRACTOR_DIR)/lib -lcomp_thumbnail
LIBS			+= -L$(DEMUXER_DIR)/lib -lcomp_demuxer
ifeq ($(CONFIG_OSS_THTTPD), y)
LIBS			+= -L$(THTTPD_DIR)/lib -lthttpd
endif
LIBS			+= -L$(OSAL_DIR)/lib -lcomp_osal
LIBS			+= -L$(SYSUTILS_DIR)/lib -lcomp_sysutils
LIBS			+= -L$(INI_DIR)/lib -lcomp_ini
LIBS			+= -L$(RTSP_DIR)/lib -lcomp_rtsp
LIBS			+= -L$(DTCF_DIR)/lib -lcomp_dtcf
LIBS			+= -L$(EXIF_DIR)/lib -lcomp_exif
LIBS			+= -L$(STORAGE_DIR)/lib -lcomp_storage
LIBS			+= -L$(UPGRADE_DIR)/lib -lcomp_upgrade
LIBS			+= -L$(LVGL_DIR)/install/$(PROCESSED_CONFIG_CROSS_COMPILE)/lib/ -llvgl -llvgl_thorvg
LIBS			+= -L$(UI_DIR)/third_party/freetype/lib/$(PROCESSED_CONFIG_CROSS_COMPILE) -lfreetype
LIBS			+= -L$(FFMPEG_DIR)/lib \
	-lavformat -lavcodec -lavutil -lswscale -lswresample
INCS			+= -I$(TDL_SDK_DIR)/include
LIBS			+= -L$(TDL_SDK_DIR)/lib
OPENCV_LIBS += -lz -lcurl -ltegra_hal -lopencv_core -lopencv_imgcodecs -lopencv_imgproc -llibopenjp2 -llibpng -llibtiff -llibwebp -littnotify
JPEG-TUBRO_LIB = -llibjpeg-turbo
KNF_LIB = -lkaldi-native-fbank-core
KISS_FFT_LIB = -lkissfft-float
ifeq ($(CONFIG_STATIC), y)
TDLSDK_LIBS := -ltdl_core-static
TPU_LIBS =  -lbmrt -lbmlib -lbmodel
else
TDLSDK_LIBS := -Wl,-Bdynamic -ltdl_core
TPU_LIBS =  -Wl,-Bdynamic -lbmrt -lbmlib -Wl,-Bstatic
endif
LIBS	+= -Wl,--start-group $(TDLSDK_LIBS) $(TPU_LIBS) $(OPENCV_LIBS) $(JPEG-TUBRO_LIB) $(KNF_LIB) $(KISS_FFT_LIB) -Wl,--end-group

INCS	+= -I$(AI_SDK_DIR)/include
LIBS	+= -L$(AI_SDK_DIR)/lib
LIBS	+=  -Wl,--start-group -limage_recognize -lface_beautifier -limg2img -lwebsockets -lvoice_chat -ltext_to_speech -lspeech_to_text -lcurl -lcrypto -lssl -Wl,--end-group

# KT_ANI_SDK libraries - stored separately, will be added to sophcam only
ifeq ($(CONFIG_COMPONENTS_KT_ANI_SDK), y)
INCS	+= -I$(KT_ANI_SDK_DIR)/include
KT_ANI_LIBS	:= -L$(KT_ANI_SDK_DIR)/lib
ifeq ($(CONFIG_STATIC), y)
# KT_ANI_SDK only provides shared libraries, force dynamic linking
KT_ANI_LIBS	+= -Wl,-Bdynamic -lkt_animal -lmodel_combine -lbmrt -lbmlib -Wl,-Bstatic
else
KT_ANI_LIBS	+= -lkt_animal -lmodel_combine -lbmrt -lbmlib
endif
endif

ifneq ($(CONFIG_STATIC), y)
ifeq ($(CONFIG_ENABLE_ISP_PQ_TOOL), y)
LIBS		+= -L$(MMF_DIR)/lib -lcvi_ispd2
LIBS		+= -L$(MMF_DIR)/lib/3rd -lcvi_json-c
endif
endif

ifeq ($(CONFIG_CV184X), y)
ifneq ($(CONFIG_STATIC), y)
LIBS			+= -L$(CVI_MPI_DIR)/lib -L$(CVI_MPI_DIR)/lib/3rd \
	-lvenc -lvdec \
	-Wl,--start-group -lsys -lipcm -lcvi_bin -lvi -lvpss -lvo -lrgn -lgdc -lisp -lawb -lae -laf -lsensor -lmipi -lini -Wl,--end-group
LIBS			+= -lcvi_audio -lcvi_dnvqe -ltinyalsa -lcvi_vqe -lcvi_RES1 -lcvi_VoiceEngine -lcvi_ssp
endif
else
LIBS			+= -L$(CVI_MPI_DIR)/lib -L$(CVI_MPI_DIR)/lib/3rd \
	-lvenc -lvdec \
	-Wl,--start-group -lsys -lmsg -lcvilink -lcvi_bin -lcvi_bin_isp -lvi -lvpss -lvo -lrgn -lgdc -lisp -lawb -lae -laf -Wl,--end-group \
 	-lcvi_audio -lcvi_vqe \
	-lcvi_VoiceEngine -lsbc -lini
LIBS			+= -lcvi_RES1 -ltinyalsa -lcvi_ssp
endif

DYN_LIBS	+= -lm -lstdc++ -lstdc++fs -pthread -ldl

PARAM_PATH_PREFIX = $(SRCTREE)/applications/$(CFG_PDT)/param/inicfgs

ifeq ($(CONFIG_APP_DASHCAM), y)
PARAM_FULL_PATH=$(PARAM_PATH_PREFIX)/$(INI_PDT)
MEDIA_PARAM_CNT   = $(shell grep -nr "config_media_cam*" $(PARAM_FULL_PATH)/config_access_entry.ini |grep -v comm |wc -l)
CFLAGS 		+= -DPARAM_MEDIA_CNT=$(MEDIA_PARAM_CNT)
MAX_DEV_NUM = $(shell grep "dev_num" $(PARAM_FULL_PATH)/config_devmng.ini | awk '{print $$3}')
CFLAGS 		+= -DMAX_DEV_INSTANCES=$(MAX_DEV_NUM)
MAX_SENSOR_NUM = $(shell grep "cam_num" $(PARAM_FULL_PATH)/config_devmng.ini | awk '{print $$3}')
CFLAGS 		+= -DMAX_CAMERA_INSTANCES=$(MAX_SENSOR_NUM)
KBUILD_DEFINES += -DMAX_CAMERA_INSTANCES=$(MAX_SENSOR_NUM)
endif

TARGET_DIR	:= $(OUTPUT)/bin
TARGET		:= $(TARGET_DIR)/ini2bin_board

SUBDIRS		:= $(CURDIR)/param/ini2bin/src
SRCS_C		:= $(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.c))
SDIR		:= $(SUBDIRS)

CFLAGS		+= -Wno-unused-parameter -Wno-cast-function-type -Wno-unused-function
# for stack trace
CFLAGS		+= -rdynamic  -funwind-tables -ffunction-sections
include $(COMMON_DIR)/common_lib.mk
########################################################################################
TARGET_DIR	:= $(OUTPUT)/bin
TARGET		:= $(TARGET_DIR)/sophcam

SUBDIRS := $(CURDIR)/param/core/src
SUBDIRS += $(CURDIR)/usbctrl/src
SUBDIRS += $(CURDIR)/media/src
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/cardv/
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/guiguider_ui
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/guiguider_ui/images
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/guiguider_ui/guider_fonts
SUBDIRS += $(CURDIR)/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/src/hal_wifi
SUBDIRS += $(CURDIR)/mode/src
ifneq ($(CONFIG_SERVICES_PHOTO), y)
exclude_src += $(SRCTREE)/applications/dashcam/mode/src/photomode.c
endif

ifeq ($(CONFIG_APPLICATION_NET), y)
SUBDIRS += $(CURDIR)/netctrl/src
ifneq ($(CONFIG_NETPROTOCOL_XML),y)
exclude_src	+= $(SRCTREE)/applications/dashcam/netctrl/src/netctrlxml.c
endif

ifneq ($(CONFIG_NETPROTOCOL_CGI),y)
exclude_src	+= $(SRCTREE)/applications/dashcam/netctrl/src/netctrlcgi.c
endif

ifneq ($(CONFIG_NETPROTOCOL_CJSON),y)
exclude_src	+= $(SRCTREE)/applications/dashcam/netctrl/src/netctrlcjson.c
exclude_src	+= $(SRCTREE)/applications/dashcam/netctrl/src/cJSON.c
endif

endif

SUBDIRS += $(CURDIR)/main

#SDIR := $(SUBDIRS)
SDIR:= $(CURDIR)
# SRCS_C += $(foreach dir,$(SUBDIRS),$(wildcard $(dir)/*.c))
SRCS_TEMP := $(shell find $(SUBDIRS) -type f -name "*.c")

SRCS_C := $(filter-out $(exclude_src), $(SRCS_TEMP))

CFLAGS += $(KBUILD_DEFINES)
CXXFLAGS += $(KBUILD_DEFINES)

# Add KT_ANI_SDK libraries only for sophcam target
LIBS += $(KT_ANI_LIBS)

include $(COMMON_DIR)/common_lib.mk

clean:
	$(call print_target)
	$(Q)rm -rf $(TARGET_DIR)
	$(Q)rm -rf $(ODIR)
