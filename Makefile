
SRCTREE		?= $(CURDIR)
COMMON_DIR	:=$(CURDIR)/common_files
OUTPUT	:= $(CURDIR)/out
INSTALL_PATH ?= $(SRCTREE)/out/install
TOP_CONFIG_FILE ?= $(SRCTREE)/.config
NPROC ?= $(shell expr $(shell nproc) - 1)

ifeq ("$(wildcard $(TOP_CONFIG_FILE))","")
ifeq ("$(filter menuconfig %defconfig clean% %clean, $(MAKECMDGOALS))","")
# if no configuration file is present and defconfig or clean
# is not a named target, run defconfig then menuconfig to get the initial config
$(error ".config is not present, make xxx_defconfig or menuconfig first")
endif
endif


-include $(TOP_CONFIG_FILE)
# 去除引号并删除末尾连字符
PROCESSED_CONFIG_CROSS_COMPILE := $(patsubst %-,%,$(subst ",,$(CONFIG_CROSS_COMPILE)))
export PROCESSED_CONFIG_CROSS_COMPILE

include $(SRCTREE)/Kbuild
include $(SRCTREE)/repo_dir_def.mk

SOPHCAM_COMMIT_ID := $(shell git rev-parse --short HEAD)
ifeq ($(strip $(SOPHCAM_COMMIT_ID)),)
SOPHCAM_COMMIT_ID := NULL
endif

export SRCTREE OUTPUT INSTALL_PATH TOP_CONFIG_FILE NPROC CROSS_COMPILE COMMON_DIR SOPHCAM_COMMIT_ID KT_ANI_SDK_DIR

# `make menuconfig`：图形化配置应用选项
# `make xxx_defconfig`：使用预设的配置文件
# `make`：编译应用
# `make install`：安装应用到 `out/install/` 目录
# `make clean`：清理除oss以外的所有编译缓存文件
# `make clean_all`：清理所有编译缓存文件（包括oss）
# `make distclean`：清理所有编译缓存文件、配置文件、安装文件
# `make release`：生成 release 包
# `make pack_data`：将应用打包到 data 分区.
TOPTARGETS := all clean clean_all distclean install pack_data release
TOPSUBDIRS := oss components app_services applications

CFLAGS += $(KBUILD_DEFINES)
CXXFLAGS += $(KBUILD_DEFINES)

define print_target
	@printf '\033[1;36;40m [TARGET][TOP] $@ \033[0m\n'
endef

all: $(TOP_CONFIG_FILE) copy_mpi_to_components copy_tdl_to_components $(TOPSUBDIRS)
	$(call print_target)

components: oss
app_services: components
applications: app_services

# 调用子目录的 Makefile
$(TOPSUBDIRS):
	$(call print_target)
	$(Q)$(MAKE) -C $@ $(MAKECMDGOALS)

install: $(TOP_CONFIG_FILE) $(TOPSUBDIRS)
	$(call print_target)
ifeq ($(or $(CONFIG_SERVICES_ADAS), $(CONFIG_SERVICES_SPEECH)), y)
	cp -a $(SRCTREE)/components/cvitek_tpu_sdk/lib/libcnpy.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tpu_sdk/lib/libcvikernel.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tpu_sdk/lib/libcvimath.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tpu_sdk/lib/libcviruntime.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tpu_sdk/lib/libz.so.* $(INSTALL_PATH)/lib/

	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//lib/libcvi_tdl.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//lib/libcvi_tdl_app.so $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_core.so.3.2 $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_imgcodecs.so.3.2 $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_imgproc.so.3.2 $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_core.so.3.2.0 $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_imgcodecs.so.3.2.0 $(INSTALL_PATH)/lib/
	cp -a $(SRCTREE)/components/cvitek_tdl_sdk//sample/3rd/opencv/lib/libopencv_imgproc.so.3.2.0 $(INSTALL_PATH)/lib/
endif

ifeq ($(CONFIG_STATIC), y)
	rm -rf $(INSTALL_PATH)/lib
	# cp $(OUTPUT)/bin/sophcam.static $(INSTALL_PATH)/bin/sophcam
	# cp $(OUTPUT)/bin/ini2bin_board.static $(INSTALL_PATH)/bin/ini2bin_board
else

	cp -rf $(SRCTREE)/../sophpi/cvitek/cvi_mpi/lib/*.so* $(INSTALL_PATH)/lib
	cp -rf $(SRCTREE)/../sophpi/cvitek/cvi_mpi/lib/3rd/*.so* $(INSTALL_PATH)/lib
	find $(INSTALL_PATH)/lib -name "*.a" -printf 'rm %p\n' -exec rm -rf {} \;
	find $(INSTALL_PATH)/lib -name "*.so*" -printf 'striping %p\n' -exec ${CROSS_COMPILE}strip --strip-all {} \;
endif

	cp -f $(OUTPUT)/bin/sophcam $(INSTALL_PATH)/bin && \
	${CROSS_COMPILE}strip $(INSTALL_PATH)/bin/sophcam
	cp -f $(OUTPUT)/bin/cc_tools $(INSTALL_PATH)/bin
	cp -rf $(SRCTREE)/components/file_recover/bin/* $(INSTALL_PATH)/bin

	mkdir -p $(INSTALL_PATH)/bin/param
	cp -rf $(SRCTREE)/applications/$(CFG_PDT)/param/inicfgs/$(INI_PDT)/* $(INSTALL_PATH)/bin/param
	cp -f $(OUTPUT)/bin/ini2bin_board $(INSTALL_PATH)/bin/param

	rm -rf $(INSTALL_PATH)/bin/res
	mkdir -p $(INSTALL_PATH)/bin/res
	cp -rf $(SRCTREE)/applications/dashcam/ui/$(CFG_PDT_SUB)/$(UI_PACKET)/res/* $(INSTALL_PATH)/bin/res

	rm -rf $(INSTALL_PATH)/bin/model
	mkdir -p $(INSTALL_PATH)/bin/model
	cp -rfL $(SRCTREE)/applications/dashcam/resource/model/$(INI_PDT)/* $(INSTALL_PATH)/bin/model

	rm -rf $(INSTALL_PATH)/bin/ai_model
	mkdir -p $(INSTALL_PATH)/bin/ai_model
	cp -rf $(SRCTREE)/oss/kt_ani_sdk/lib/ $(INSTALL_PATH)/bin/ai_model
	cp -rf $(SRCTREE)/oss/kt_ani_sdk/bin/user_config.json $(INSTALL_PATH)/bin/ai_model
	cp -rf $(SRCTREE)/oss/kt_ani_sdk/bin/resource/* $(INSTALL_PATH)/bin/ai_model

clean: $(TOPSUBDIRS)
	$(call print_target)
	rm -rf $(OBJTREE)
	rm -rf $(INSTALL_PATH)
	rm -rf out/*

clean_all: clean
	$(call print_target)

distclean:
	$(call print_target)
	$(MAKE) clean
	rm .config
	rm -rf out

DESTINATION_MPI_DIR := $(SRCTREE)/components/comps_install/$(PROCESSED_CONFIG_CROSS_COMPILE)/cvi_mpi
CVI_MPI_DIR := $(CONFIG_CVI_PLATFORM_DIR)/cvi_mpi

copy_mpi_to_components:
	$(call print_target)
ifneq ($(CVI_MPI_DIR),)
	$(Q)[ -d $(DESTINATION_MPI_DIR) ] && rm -rf $(DESTINATION_MPI_DIR) || true
	$(Q)mkdir -p $(DESTINATION_MPI_DIR)
	$(Q)cp -r $(CVI_MPI_DIR)/include $(DESTINATION_MPI_DIR)/include
	$(Q)cp -r $(CVI_MPI_DIR)/lib $(DESTINATION_MPI_DIR)/lib
endif

DESTINATION_TDL_DIR := $(SRCTREE)/components/comps_install/$(PROCESSED_CONFIG_CROSS_COMPILE)/cvi_tdl_sdk
CVI_TDL_DIR := $(CONFIG_CVI_PLATFORM_DIR)/tdl_sdk
CVI_LIBSOPHON_LIB_DIR := $(CONFIG_CVI_PLATFORM_DIR)/libsophon/install/libsophon-0.4.9/lib
OPENCV_LIB_DIR := $(CVI_TDL_DIR)/build/CV184X/_deps/opencv-src/lib
OPENCV_3RD_LIB_DIR := $(CVI_TDL_DIR)/build/CV184X/_deps/opencv-src/lib/opencv4/3rdparty
ZLIB_DIR := $(CVI_TDL_DIR)/build/CV184X/_deps/zlib-src/lib
KNF_LIB := $(CVI_TDL_DIR)/build/CV184X/_deps/kaldi-native-fbank-build/libkaldi-native-fbank-core.a
KISS_FFT_LIB := $(CVI_TDL_DIR)/build/CV184X/_deps/kissfft-build/libkissfft-float.a

copy_tdl_to_components:
	$(call print_target)
ifneq ($(CVI_TDL_DIR),)
	$(Q)[ -d $(DESTINATION_TDL_DIR) ] && rm -rf $(DESTINATION_TDL_DIR) || true
	$(Q)mkdir -p $(DESTINATION_TDL_DIR)
	$(Q)cp -r $(CVI_TDL_DIR)/install/CV184X/include $(DESTINATION_TDL_DIR)/include
	$(Q)cp -r $(CVI_TDL_DIR)/install/CV184X/lib $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(CVI_LIBSOPHON_LIB_DIR)/* $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(OPENCV_LIB_DIR)/* $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(OPENCV_3RD_LIB_DIR)/* $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(ZLIB_DIR)/* $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(KNF_LIB) $(DESTINATION_TDL_DIR)/lib
	$(Q)cp -r $(KISS_FFT_LIB) $(DESTINATION_TDL_DIR)/lib
endif

.PHONY: $(TOPTARGETS) $(TOPSUBDIRS)

menuconfig:
	$(Q)$(MAKE) -f Makefile.kbuild $@

%_defconfig:
	$(Q)$(MAKE) -f Makefile.kbuild $@

pack_data:
	$(call print_target)
	if [ -f build_cvitek.sh ];then chmod +x ./build_cvitek.sh;./build_cvitek.sh $(MAKECMDGOALS) $(CONFIG_CVI_BUILD_DIRNAME) $(CONFIG_CVI_PLATFORM_DIR);fi

release:
	$(call print_target)
	@cd $(SRCTREE)/release && \
	if [ -f release.sh ]; then \
		chmod +x ./release.sh && \
		bash ./release.sh $(CONFIG_RELEASE_DEFCONFIG_FILE); \
	fi
