#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/prctl.h>

#include "appcomm.h"
#include "media_init.h"
#include "audio_service.h"
#include "recordmng.h"
#ifdef SERVICES_PHOTO_ON
#include "photomng.h"
#endif
#include "hal_screen_comp.h"
#include "cvi_ae.h"
#include "cvi_awb.h"
#include "cvi_af.h"
#include "param.h"
#include "system.h"
#include "media_dump.h"
#include "sysutils_eventhub.h"
#include "hal_pwm.h"
#include "mode.h"
#ifdef SERVICES_LIVEVIEW_ON
#include "volmng.h"
#endif
// #include "liveview.h"

#ifdef GPS_ON
#include "gpsmng.h"
static GPSMNG_CALLBACK gstGPSCallback = {0};
static RECORD_SERVICE_GPS_INFO_S gstGPSInfo = {0};
static pthread_mutex_t gGPSMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef ENABLE_VIDEO_MD
#include "cvi_videomd.h"
#define IVE_KO_PATH KOMOD_PATH "/" CHIP_TYPE "_ive.ko"
#endif

#ifdef SERVICES_ADAS_ON
#include "adasmng.h"
#endif

#ifdef SERVICES_RTSP_ON
#include "rtsp_service.h"
#endif

#ifdef SERVICES_SUBVIDEO_ON
#include "subvideo_service.h"
#endif

#include "storagemng.h"

typedef struct {
	MAPI_VENC_CHN_ATTR_T venc_attr;
#ifdef SERVICES_RTSP_ON
	RTSP_SERVICE_PARAM_S *param;
#endif
	pthread_mutex_t mutex;
} RTSP_VENC_CTX;

RTSP_VENC_CTX rtsp_venc_ctx[MAX_RTSP_CNT] = {0};

static MEDIA_PARAM_INIT_S SysMediaParams;

MEDIA_PARAM_INIT_S *MEDIA_GetCtx(void)
{
	return &SysMediaParams;
}

static int32_t MEDIA_InitVproc(PARAM_MEDIA_VPROC_GRP_ATTR_S *mediaVprocAttr, MAPI_VPROC_ATTR_T *mapiVprocAttr)
{
	uint32_t i = 0;
	memcpy(&mapiVprocAttr->attr_inp, &(mediaVprocAttr->VpssGrpAttr), sizeof(VPSS_GRP_ATTR_S));
	memcpy(&mapiVprocAttr->crop_info, &(mediaVprocAttr->VpssCropInfo), sizeof(VPSS_CROP_INFO_S));

	for (i = 0; i < mediaVprocAttr->ChnCnt && (i < MAPI_VPROC_MAX_CHN_NUM); i++) {
		memcpy(&(mapiVprocAttr->attr_chn[i]), &(mediaVprocAttr->VprocChnAttr[i].VpssChnAttr), sizeof(VPSS_CHN_ATTR_S));
		memcpy(&(mapiVprocAttr->chn_bufWrap[i]), &(mediaVprocAttr->VprocChnAttr[i].VpssBufWrap), sizeof(VPSS_CHN_BUF_WRAP_S));
		memcpy(&(mapiVprocAttr->chn_cropInfo[i]), &(mediaVprocAttr->VprocChnAttr[i].VpssChnCropInfo), sizeof(VPSS_CROP_INFO_S));
		mapiVprocAttr->chn_vbcnt[i] = mediaVprocAttr->VprocChnAttr[i].VprocChnVbCnt;
		mapiVprocAttr->lowdelay_cnt[i] = mediaVprocAttr->VprocChnAttr[i].VprocChnLowDelayCnt;
		mapiVprocAttr->chn_num++;
		if (mediaVprocAttr->VprocChnAttr[i].VprocChnEnable == true) {
			mapiVprocAttr->chn_enable[i] = 1;
		}else {
			mapiVprocAttr->chn_enable[i] = 0;
		}
	}
	mapiVprocAttr->attr_inp.u8VpssDev = mediaVprocAttr->VpssDev;
	return 0;
}

bool MEDIA_Is_CameraEnabled(int32_t cam_index)
{
	bool is_enabled = false;
	PARAM_GetCamStatus(cam_index, &is_enabled);
	return is_enabled;
}

uint32_t MEDIA_Res2RecordMediaMode(int32_t res)
{
	uint32_t MediaSize = 0;

	MediaSize = res;

	return MediaSize;
}

int32_t MEDIA_Res2PhotoMediaMode(int32_t res)
{
    int32_t MediaSize = 0;

    switch (res) {
    case MEDIA_PHOTO_SIZE_VGA:
        MediaSize = MEDIA_PHOTO_SIZE_640X480P;
        break;
    case MEDIA_PHOTO_SIZE_2M:
        MediaSize = MEDIA_PHOTO_SIZE_1920X1080P;
        break;
    case MEDIA_PHOTO_SIZE_4M:
        MediaSize = MEDIA_PHOTO_SIZE_2688X1512P;
        break;
    case MEDIA_PHOTO_SIZE_5M:
        MediaSize = MEDIA_PHOTO_SIZE_2592X1944P;
        break;
    case MEDIA_PHOTO_SIZE_8M:
        MediaSize = MEDIA_PHOTO_SIZE_3840X2160P;
        break;
    case MEDIA_PHOTO_SIZE_12M:
        MediaSize = MEDIA_PHOTO_SIZE_4000X3000P;
        break;
    case MEDIA_PHOTO_SIZE_16M:
        MediaSize = MEDIA_PHOTO_SIZE_5760X3240P;
        break;
    case MEDIA_PHOTO_SIZE_24M:
        MediaSize = MEDIA_PHOTO_SIZE_5600X4200P;
        break;
    case MEDIA_PHOTO_SIZE_32M:
        MediaSize = MEDIA_PHOTO_SIZE_7680X4320P;
        break;
    case MEDIA_PHOTO_SIZE_36M:
        MediaSize = MEDIA_PHOTO_SIZE_8064X4536P;
        break;
    case MEDIA_PHOTO_SIZE_48M:
        MediaSize = MEDIA_PHOTO_SIZE_8192X6144P;
        break;
    case MEDIA_PHOTO_SIZE_64M:
        MediaSize = MEDIA_PHOTO_SIZE_8192X8192P;
        break;
    default:
        MediaSize = -1;
        CVI_LOGE("error resolution: %d !", res);
        break;
    }

    return MediaSize;
}

int32_t MEDIA_Size2PhotoMediaMode(int32_t width, int32_t height, const char *desc)
{
    (void)height;
    int media_mode = 0;
    if (width == 8192) {
        media_mode = MEDIA_PHOTO_SIZE_8192X6144P;
    } else if (width == 8064) {
        media_mode = MEDIA_PHOTO_SIZE_8064X4536P;
    } else if (width == 8000) {
        media_mode = MEDIA_PHOTO_SIZE_8000X6000P;
    } else if (width == 7680) {
        media_mode = MEDIA_PHOTO_SIZE_7680X4320P;
    } else if (width == 5760) {
        media_mode = MEDIA_PHOTO_SIZE_5760X3240P;
    } else if (width == 5600) {
        media_mode = MEDIA_PHOTO_SIZE_5600X4200P;
    } else if (width == 4608) {
        media_mode = MEDIA_PHOTO_SIZE_4608X3456P;
    } else if (width == 4000) {
        media_mode = MEDIA_PHOTO_SIZE_4000X3000P;
    } else if (width == 3840) {
        media_mode = MEDIA_PHOTO_SIZE_3840X2160P;
    } else if (width == 2688) {
        media_mode = MEDIA_PHOTO_SIZE_2688X1512P;
    } else if (width == 2592) {
        media_mode = MEDIA_PHOTO_SIZE_2592X1944P;
    } else if (width == 1920) {
        if (desc != NULL && (strstr(desc, "Crop-2M") != NULL)) {
            media_mode = MEDIA_PHOTO_SIZE_1920X1080P_NEW;
        } else {
            media_mode = MEDIA_PHOTO_SIZE_1920X1080P;
        }
    } else if (width == 640) {
        media_mode = MEDIA_PHOTO_SIZE_640X480P;
    }
    CVI_LOGI("media_mode: %d\n", media_mode);
    return media_mode;
}

/**
 * @brief 从"显示名@帧率"格式中提取帧率
 * @param full_str 完整字符串（格式："显示名加帧率"）
 * @param desc_buf 输出缓冲区，存储帧率字符
 * @param buf_size 缓冲区大小
 * @note 去除第一个空格后的部分即为显示名
 */
static void extract_fps(const char *full_str, char *desc_buf, size_t buf_size)
{
    if (!full_str || !desc_buf || buf_size == 0) {
        return;
    }

    const char *space_pos = strchr(full_str, '@');
    if (space_pos != NULL) {
        /* 找到第一个@，跳过它，后面的就是显示名 */
        strncpy(desc_buf, space_pos + 1, buf_size - 1);
        desc_buf[buf_size - 1] = '\0';
    } else {
        /* 没有@，整个字符串就是显示名*/
        strncpy(desc_buf, full_str, buf_size - 1);
        desc_buf[buf_size - 1] = '\0';
    }
}

static uint32_t get_shot_count_value(char *desc_buf)
{
    char *endptr;
    unsigned long value = strtoul(desc_buf, &endptr, 10);

    // 检查转换是否有效
    if (*endptr != '\0' ||        // 存在非法字符
        endptr == desc_buf || // 空字符串
        value > UINT16_MAX)       // 超过uint16_t范围
    {
        // CVI_ERR("Invalid shot count: %s", desc_buf);
        return 25; // 返回默认值
    }

    return (uint32_t)value;
}

int32_t MEDIA_Size2VideoMediaMode(int32_t width, int32_t height,char *str)
{
	(void)height;
	int media_mode = 0;
	char item_fps[32] = {0};
	uint32_t fps = 0;

	/* Crop-2M (new sensor); keep legacy "1080P_NEW" strings working. */
	if (str != NULL && width == 1920 &&
	    (strstr(str, "Crop-2M") != NULL || strstr(str, "1080p_new") != NULL)) {
		return MEDIA_VIDEO_SIZE_1920X1080_NEW;
	}

	extract_fps(str, item_fps, sizeof(item_fps));//分离帧率字符
	// CVI_LOGI("bug调试 item_fps: %s\n", item_fps);
	fps = get_shot_count_value(item_fps);//将字符转换成数字
	// CVI_LOGI("bug调试 fps: %d\n", fps);

	if (width == 3840) {
		media_mode = MEDIA_VIDEO_SIZE_3840X2160P25;
	} else if (width == 2688) {
		media_mode = MEDIA_VIDEO_SIZE_2688X1512P25;
	} else if (width == 1920  && fps == 30) {
		media_mode = MEDIA_VIDEO_SIZE_1920X1080P25;
	} else if (width == 1920 && fps == 60) {
		media_mode = MEDIA_VIDEO_SIZE_1920X1080P60;
	}else if (width == 1280) {
		media_mode = MEDIA_VIDEO_SIZE_1280X720P60;
	} else if (width == 640) {
		media_mode = MEDIA_VIDEO_SIZE_640X480P25;
	}
	CVI_LOGI("media_mode: %d\n", media_mode);
	return media_mode;
}

static int32_t MEDIA_SensorSetRes(int32_t snsid, int32_t mode)
{
	if (mode != AHD_MODE_NONE) {
		PARAM_CFG_S param;
		PARAM_GetParam(&param);
		param.WorkModeCfg.RecordMode.CamMediaInfo[snsid].CurMediaMode = MEDIA_Res2RecordMediaMode(mode);
		param.WorkModeCfg.PhotoMode.CamMediaInfo[snsid].CurMediaMode = MEDIA_Res2PhotoMediaMode(mode);
		PARAM_SetParam(&param);
	}
	return 0;
}

static int32_t MEDIA_SensorPlugCallback(int32_t snsid, int32_t mode)
{
	CVI_LOGI("Sensor %d mode :%d\n", snsid, mode);
	EVENT_S stEvent;
	stEvent.aszPayload[0] = mode;
	stEvent.aszPayload[1] = snsid;

	if (mode == AHD_MODE_NONE) {
		stEvent.arg1 = SENSOR_PLUG_OUT;
	} else {
		stEvent.arg1 = SENSOR_PLUG_IN;
	}
	stEvent.topic = EVENT_SENSOR_PLUG_STATUS;
	stEvent.arg2 = 1;
	EVENTHUB_Publish(&stEvent);
	return 0;
}

int32_t MEDIA_SensorDet(void)
{
	int32_t mode = AHD_MODE_NONE;
	int32_t status = 0;
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);

		// MAPI_VCAP_GetAhdMode(i, &mode, &status, params.SnsAttr.SnsChnAttr.u32sensortype);

		status = 1; //todo
		PARAM_SetCamStatus(i, status);

		printf("i(%d),status(%d) \n", i, status);
		if (i == 0) {
			EVENTHUB_RegisterTopic(EVENT_SENSOR_PLUG_STATUS);
		}
		/*RGB SENSOR mode == AHD_MODE_BUIT*/
		if (mode != -1) { /*YUV SENSOR*/
			if (mode == AHD_MODE_NONE) {
				PARAM_GetAhdDefaultMode(i, &mode);
				CVI_LOGI("Ahd Default Mode: %d\n", mode);
			}
			MEDIA_SensorSetRes(i, mode);
			// MAPI_VCAP_SetAhdMode(i, mode);
			// MAPI_VCAP_InitSensorDetect(i, (void *)MEDIA_SensorPlugCallback);
		}

#ifdef SERVICES_LIVEVIEW_ON
		PARAM_WND_ATTR_S *WndParam = &PARAM_GetCtx()->pstCfg->MediaComm.Window;
		// WndParam->Wnds[i].WndEnable = status;
		WndParam->Wnds[i].WndEnable = 1; //todo MAPI_VCAP_GetAhdMode获得的statu不正确
#endif
	}

	return 0;
}

int32_t MEDIA_SensorInit(void)
{
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	MAPI_VCAP_ATTR_T vcap_attr;
	int32_t s32Ret = 0;
	int32_t status = 0;
	s32Ret = MAPI_VCAP_GetGeneralVcapAttr(&vcap_attr, MAX_CAMERA_INSTANCES);

	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_GetGeneralVcapAttr fail");

	/*
	 *  user can select diffrent params base on genral attribute,
	 *  such as sensor size/wdr mode/compress mode/i2c addr...
	 */
	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		memset(&params, 0x0, sizeof(PARAM_MEDIA_SPEC_S));
		PARAM_GetMediaMode(i, &params);
		memcpy(&vcap_attr.attr_chn[i], &(params.VcapAttr.VcapChnAttr), sizeof(MAPI_VCAP_CHN_ATTR_T));
		memcpy(&vcap_attr.attr_sns[i], &(params.SnsAttr.SnsChnAttr), sizeof(MAPI_VCAP_SENSOR_ATTR_T));
		status++;
	}
	vcap_attr.u8DevNum = status;

#ifdef CHIP_184X
	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);

		s32Ret = MAPI_VCAP_InitSensor(&Syshdl->sns[i], params.SnsAttr.SnsChnAttr.u8SnsId, &vcap_attr);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_InitSensor fail");

		// if (i == 0) {
		// 	s32Ret = MAPI_VCAP_SetPqBinPath(Syshdl->sns[i]);
		// 	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetPqBinPath fail");
		// }
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_MipiReset(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_MipiReset fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_SetMipiAttr(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetMipiAttr fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_SetSensorClock(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetSensorClock fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_SetSensorReset(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetSensorReset fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_SetSnsProbe(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetSnsProbe fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif
				s32Ret = MAPI_VCAP_StartDev(Syshdl->sns[i]);
				MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartDev fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif
		s32Ret = MAPI_VCAP_StartPipe(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartPipe fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_SetPqBinPath(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetPqBinPath fail");

		s32Ret = MAPI_VCAP_InitISP(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_InitISP fail");

		s32Ret = MAPI_VCAP_SetISP(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetISP fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif
		s32Ret = MAPI_VCAP_SetSnsInit(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetSnsInit fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
		#endif

		s32Ret = MAPI_VCAP_StartChn(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartChn fail");
	}
#else
	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);

		s32Ret = MAPI_VCAP_InitSensor(&Syshdl->sns[i], params.SnsAttr.SnsChnAttr.u8SnsId, &vcap_attr);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_InitSensor fail");

		s32Ret = MAPI_VCAP_StartDev(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartDev fail");
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		s32Ret = MAPI_VCAP_StartPipe(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartPipe fail");

		s32Ret = MAPI_VCAP_InitISP(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_InitISP fail");
		s32Ret = MAPI_VCAP_StartChn(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StartChn fail");
	}
#endif


	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);
		MAPI_VCAP_MIRRORFLIP_ATTR_S Attr;
		Attr.bFlip = params.VcapAttr.VcapChnAttr.bFlip;
		Attr.bMirror = params.VcapAttr.VcapChnAttr.bMirror;
		MAPI_VCAP_SetAttrEx(Syshdl->sns[i], MAPI_VCAP_CMD_MirrorFlip, (void *)&Attr, sizeof(MAPI_VCAP_MIRRORFLIP_ATTR_S));
		// set fps
		float fps = params.VcapAttr.VcapChnAttr.f32Fps;
		MAPI_VCAP_SetAttrEx(Syshdl->sns[i], MAPI_VCAP_CMD_Fps, (void *)&fps, sizeof(float));
	}

	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		status = 0;
		MAPI_VCAP_GetSensorPipeAttr(Syshdl->sns[i], &status);

		PARAM_SetCamIspInfoStatus(i, status);
	}
// 	s32Ret = MEDIA_SetAntiFlicker();
// 	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_SetAntiFlicker fail");

	return 0;
}

int32_t MEDIA_SensorDeInit(void)
{
	int32_t s32Ret = 0;

	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	for (uint32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		if (Syshdl->sns[i] == NULL) {
			continue;
		}

		s32Ret = MAPI_VCAP_GetISP(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_GetISP fail");

		s32Ret = MAPI_VCAP_DeInitISP(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_DeInitISP fail");

		s32Ret = MAPI_VCAP_StopChn(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StopChn fail");

		s32Ret = MAPI_VCAP_StopPipe(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StopPipe fail");

		s32Ret = MAPI_VCAP_StopDev(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_StopDev fail");

		s32Ret = MAPI_VCAP_MipiReset(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_MipiReset fail");

		s32Ret = MAPI_VCAP_DeinitSensor(Syshdl->sns[i]);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_DeinitSensor fail");

		Syshdl->sns[i] = NULL;
	}
	return 0;
}

static int32_t MEDIA_VprocInit(void)
{
	int32_t s32Ret = 0;
	PARAM_WORK_MODE_S Workmode;
	PARAM_GetWorkModeParam(&Workmode);
	PARAM_VPSS_ATTR_S Vpssmode = {0};
	int32_t s32CurMode = MODEMNG_GetCurWorkMode();
	if (WORK_MODE_MOVIE == s32CurMode) {
		memcpy(&Vpssmode, &Workmode.RecordMode.Vpss, sizeof(PARAM_VPSS_ATTR_S));
	} else if (WORK_MODE_PHOTO == s32CurMode) {
		memcpy(&Vpssmode, &Workmode.PhotoMode.Vpss, sizeof(PARAM_VPSS_ATTR_S));
	} else {
		CVI_LOGE("This mode has no vpss parameters!\n");
	}
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_MEDIA_VPROC_GRP_ATTR_S *pGrpAttr = NULL;
		PARAM_GetMediaMode(i, &params);

		/* Update VI fps during vproc/vpss re-init without rebuilding VI. */
		float fps = params.VcapAttr.VcapChnAttr.f32Fps;
		s32Ret = MAPI_VCAP_SetAttrEx(Syshdl->sns[i], MAPI_VCAP_CMD_Fps, (void *)&fps, sizeof(float));
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VCAP_SetAttrEx fps fail");
		CVI_LOGI("apply cam%u vi fps=%f", i, fps);

		for (uint32_t j = 0; j < params.VprocAttr.VprocCnt; j++) {
			pGrpAttr = &params.VprocAttr.VprocGrpAttr[j];
			if (pGrpAttr->VprocEnable == false) {
				continue;
			}

			MAPI_VPROC_ATTR_T vproc_attr;
			memset((void *)&vproc_attr, 0, sizeof(vproc_attr));

			s32Ret = MEDIA_InitVproc(pGrpAttr, &vproc_attr);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_InitVproc fail");

			s32Ret = MAPI_VPROC_Init(&Syshdl->vproc[pGrpAttr->Vprocid], pGrpAttr->Vprocid, &vproc_attr);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_Init fail");
			for (uint32_t k = 0; k < pGrpAttr->ChnCnt; k++) {
				if (pGrpAttr->VprocChnAttr[k].VprocChnEnable == false)
					continue;
				if (pGrpAttr->VprocChnAttr[k].enRotation != ROTATION_0) {
					s32Ret = MAPI_VPROC_SetChnRotation(Syshdl->vproc[pGrpAttr->Vprocid], k, pGrpAttr->VprocChnAttr[k].enRotation);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_SetChnRotation fail");
				}
			}

			if (pGrpAttr->BindEnable == true) {
				if (pGrpAttr->stSrcChn.enModId == CVI_ID_VPSS) {
					s32Ret = MAPI_VPROC_BindVproc(Syshdl->vproc[pGrpAttr->stSrcChn.s32DevId], pGrpAttr->stSrcChn.s32ChnId,
												  Syshdl->vproc[pGrpAttr->Vprocid]);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_BindVproc fail");
				} else if (pGrpAttr->stSrcChn.enModId == CVI_ID_VI) {
					if (Vpssmode.stVIVPSSMode.aenMode[i] == VI_OFFLINE_VPSS_ONLINE ||
						Vpssmode.stVIVPSSMode.aenMode[i] == VI_ONLINE_VPSS_ONLINE) {
						continue;
					}

					uint8_t id = (MAX_DEV_INSTANCES == 1) ? 0 : i;
					s32Ret = MAPI_VPROC_BindVcap(Syshdl->vproc[pGrpAttr->Vprocid], Syshdl->sns[id], i);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_BindVcap fail");

				}
			}
		}
	}

	return 0;
}

static int32_t MEDIA_VprocDeInit(void)
{
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	PARAM_WORK_MODE_S Workmode = {0};
	PARAM_GetWorkModeParam(&Workmode);
	PARAM_VPSS_ATTR_S Vpssmode = {0};
	int32_t s32CurMode = MODEMNG_GetCurWorkMode();
	if (WORK_MODE_MOVIE == s32CurMode) {
		memcpy(&Vpssmode, &Workmode.RecordMode.Vpss, sizeof(PARAM_VPSS_ATTR_S));
	} else if (WORK_MODE_PHOTO == s32CurMode) {
		memcpy(&Vpssmode, &Workmode.PhotoMode.Vpss, sizeof(PARAM_VPSS_ATTR_S));
	} else {
		CVI_LOGE("This mode(%d) has no vpss parameters!\n", s32CurMode);
	}

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);
		for (int32_t j = params.VprocAttr.VprocCnt - 1; j >= 0; j--) {
			if (params.VprocAttr.VprocGrpAttr[j].VprocEnable == false) {
				continue;
			}
			if (params.VprocAttr.VprocGrpAttr[j].BindEnable == true) {
				if (params.VprocAttr.VprocGrpAttr[j].stSrcChn.enModId == CVI_ID_VPSS) {
					s32Ret = MAPI_VPROC_UnBindVproc(Syshdl->vproc[params.VprocAttr.VprocGrpAttr[j].stSrcChn.s32DevId], params.VprocAttr.VprocGrpAttr[j].stSrcChn.s32ChnId,
													Syshdl->vproc[params.VprocAttr.VprocGrpAttr[j].Vprocid]);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_UnBindVproc fail");
				} else if (params.VprocAttr.VprocGrpAttr[j].stSrcChn.enModId == CVI_ID_VI) {
					if (Vpssmode.stVIVPSSMode.aenMode[i] != VI_OFFLINE_VPSS_ONLINE &&
						Vpssmode.stVIVPSSMode.aenMode[i] != VI_ONLINE_VPSS_ONLINE) {
						uint8_t id = (MAX_DEV_INSTANCES == 1) ? 0 : i;
						uint8_t vichn_id = (MAX_DEV_INSTANCES == 1) ? 0 : i;
						s32Ret = MAPI_VPROC_UnBindVcap(Syshdl->vproc[params.VprocAttr.VprocGrpAttr[j].Vprocid], Syshdl->sns[id], vichn_id);
						MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_UnBindVcap fail");
					}
				}
			}
			s32Ret = MAPI_VPROC_Deinit(Syshdl->vproc[params.VprocAttr.VprocGrpAttr[j].Vprocid]);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_Deinit fail");
			Syshdl->vproc[params.VprocAttr.VprocGrpAttr[j].Vprocid] = NULL;
		}
	}

	return 0;
}

int32_t MEDIA_MD_Init(void)
{
#if defined(ENABLE_VIDEO_MD)
	OSAL_FS_Insmod(IVE_KO_PATH, NULL);

	int32_t s32Ret = 0;
	CVI_MOTION_DETECT_ATTR_S mdAttr;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	PARAM_MD_ATTR_S Md = {0};
	PARAM_GetMdConfigParam(&Md);
	CVI_LOGD(" Md->motionSensitivity = %d\n", Md.motionSensitivity);

	mdAttr.threshold = Md.motionSensitivity;

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);
		mdAttr.camid = i;

		for (int32_t j = 0; j < MAPI_VPROC_MAX_CHN_NUM; j++) {
			if (params.VprocAttr.VprocChnAttr[j].VprocChnEnable == true && params.VprocAttr.VprocChnAttr[j].VprocChnid == Md.ChnAttrs[i].BindVprocChnId) {
				mdAttr.isExtVproc = 0;
				mdAttr.w = params.VprocAttr.VprocChnAttr[j].VpssChnAttr.u32Width;
				mdAttr.h = params.VprocAttr.VprocChnAttr[j].VpssChnAttr.u32Height;
			} else if (params.VprocAttr.ExtChnAttr[j].ChnEnable == true && params.VprocAttr.ExtChnAttr[j].ChnAttr.ChnId == Md.ChnAttrs[i].BindVprocChnId) {
				mdAttr.isExtVproc = 1;
				mdAttr.w = params.VprocAttr.ExtChnAttr[j].ChnAttr.VpssChnAttr.u32Width;
				mdAttr.h = params.VprocAttr.ExtChnAttr[j].ChnAttr.VpssChnAttr.u32Height;
			} else {
				continue;
			}
		}

		mdAttr.vprocChnId = Md.ChnAttrs[i].BindVprocChnId;
		for (int32_t z = 0; z < MAX_VPROC_CNT; z++) {
			if (Syshdl->vproc[z] != NULL && Md.ChnAttrs[i].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(Syshdl->vproc[z])) {
				mdAttr.vprocHandle = Syshdl->vproc[z];
				break;
			}
		}

		mdAttr.state = Md.ChnAttrs[i].Enable;
		s32Ret = CVI_MOTION_DETECT_Init(&mdAttr);
		if (s32Ret != 0) {
			MEDIA_CHECK_RET(s32Ret, s32Ret, "MEDIA_VIDEOMD_Init fail");
			return s32Ret;
		}
	}
#endif

	return 0;
}

int32_t MEDIA_MD_DeInit(void)
{
#if defined(ENABLE_VIDEO_MD)
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		CVI_MOTION_DETECT_DeInit(i);
	}
	OSAL_FS_Rmmod(IVE_KO_PATH);
#endif

	return 0;
}

int32_t MEDIA_VencInit(void)
{
	int32_t j = 0;
	int32_t s32Ret = 0;
	PARAM_MEDIA_COMM_S mediaparams;
	PARAM_GetMediaComm(&mediaparams);

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		memset(&params, 0x0, sizeof(PARAM_MEDIA_SPEC_S));
		MAPI_VENC_CHN_ATTR_T venc_attr = {0};
		PARAM_GetMediaMode(i, &params);
		for (j = 0; j < MAX_VENC_CNT; j++) {
			if (params.VencAttr.VencChnAttr[j].VencChnEnable == true) {
				memcpy(&venc_attr.venc_param, &params.VencAttr.VencChnAttr[j].MapiVencAttr, sizeof(MAPI_VENC_CHN_PARAM_T));
				venc_attr.sbm_enable = params.VencAttr.VencChnAttr[j].sbm_enable > 0 ? CVI_TRUE : CVI_FALSE;
				venc_attr.BindVprocId = params.VencAttr.VencChnAttr[j].BindVprocId;
				venc_attr.BindVprocChnId = params.VencAttr.VencChnAttr[j].BindVprocChnId;
#ifndef SERVICES_SUBVIDEO_ON
	#ifdef SERVICES_RTSP_ON
				PARAM_RTSP_CHN_ATTR_S *rtspattr = &mediaparams.Rtsp.ChnAttrs[i];
				if (rtspattr->BindVencId == params.VencAttr.VencChnAttr[j].VencId) {
					memcpy(&rtsp_venc_ctx[i].venc_attr, &venc_attr, sizeof(MAPI_VENC_CHN_ATTR_T));
				} else
	#endif
				{
					MAPI_VENC_HANDLE_T *venc_hdl = &MEDIA_GetCtx()->SysHandle.venchdl[i][j];
					s32Ret = MAPI_VENC_InitChn(venc_hdl, &venc_attr);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_InitChn fail");

					// V4.2.0 does not exist for these two interfaces
					// if((params.VencAttr.VencChnAttr[j].MapiVencAttr.codec == MAPI_VCODEC_H264)
					// || (params.VencAttr.VencChnAttr[j].MapiVencAttr.codec == MAPI_VCODEC_H265)){
					//     s32Ret = MAPI_VENC_SetDataFifoLen(*venc_hdl, params.VencAttr.VencChnAttr[j].MapiVencAttr.datafifoLen);
					//     MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_SetDataFifoLen fail");
					//     uint32_t datafifoLen = 0;
					//     s32Ret = MAPI_VENC_GetDataFifoLen(*venc_hdl, &datafifoLen);
					//     MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_GetDataFifoLen fail");
					//     CVI_LOGI("snsid %u chnid %d datafifoLen %u", i, MAPI_VENC_GetChn(*venc_hdl), datafifoLen);
					// }
				}
#else
	#ifdef SERVICES_RTSP_ON
                PARAM_RTSP_CHN_ATTR_S       *rtspattr = &mediaparams.Rtsp.ChnAttrs[i];
                if (rtspattr->BindVencId == params.VencAttr.VencChnAttr[j].VencId) {
                    memcpy(&rtsp_venc_ctx[i].venc_attr, &venc_attr, sizeof(MAPI_VENC_CHN_ATTR_T));
                }
	#endif
                MAPI_VENC_HANDLE_T *venc_hdl = &MEDIA_GetCtx()->SysHandle.venchdl[i][j];
                s32Ret = MAPI_VENC_InitChn(venc_hdl, &venc_attr);
                MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_InitChn fail");
                // if((params.VencAttr.VencChnAttr[j].MapiVencAttr.codec == MAPI_VCODEC_H264)
                // || (params.VencAttr.VencChnAttr[j].MapiVencAttr.codec == MAPI_VCODEC_H265)){
                //     s32Ret = MAPI_VENC_SetDataFifoLen(*venc_hdl, params.VencAttr.VencChnAttr[j].MapiVencAttr.datafifoLen);
                //     MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_SetDataFifoLen fail");
                //     uint32_t datafifoLen = 0;
                //     s32Ret = MAPI_VENC_GetDataFifoLen(*venc_hdl, &datafifoLen);
                //     MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_GetDataFifoLen fail");
                //     CVI_LOGI("snsid %u chnid %d datafifoLen %u", i, MAPI_VENC_GetChn(*venc_hdl), datafifoLen);
                // }
	#endif
			}
		}
	}

	return 0;
}

int32_t MEDIA_VencDeInit(void)
{
	int32_t j = 0;
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);

		for (j = 0; j < MAX_VENC_CNT; j++) {
			if (Syshdl->venchdl[i][j] != NULL) {
				if (params.VencAttr.VencChnAttr[j].sbm_enable) {
					s32Ret = MAPI_VENC_UnBindVproc(Syshdl->venchdl[i][j], params.VencAttr.VencChnAttr[j].BindVprocId, params.VencAttr.VencChnAttr[j].BindVprocChnId);
					MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_UnBindVproc fail");
				}
				MAPI_VENC_StopRecvFrame(Syshdl->venchdl[i][j]);
				s32Ret = MAPI_VENC_DeinitChn(Syshdl->venchdl[i][j]);
				MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VENC_DeinitChn fail");
				Syshdl->venchdl[i][j] = NULL;
			}
		}
	}

	return 0;
}

int32_t MEDIA_VideoInit(void)
{
	int32_t s32Ret = 0;

#ifdef ENABLE_ISP_PQ_TOOL
	MEDIA_DUMP_ReplayInit();
#endif
	// s32Ret = MEDIA_SensorInit();
	// MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_SensorInit fail");

	s32Ret = MEDIA_VprocInit();
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_VprocInit fail");

	return 0;
}

int32_t MEDIA_VideoDeInit(void)
{
	int32_t s32Ret = 0;

	// s32Ret = MEDIA_SensorDeInit();
	// MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_SensorDeInit fail");

	s32Ret = MEDIA_VprocDeInit();
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_VprocDeInit fail");

	return 0;
}

int32_t MEDIA_VbInit(void)
{
	MAPI_MEDIA_SYS_ATTR_T sys_attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetVbParam(&sys_attr);

	s32Ret = MAPI_SYS_VB_Init(&sys_attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_SYS_VB_Init fail");

	return 0;
}

int32_t MEDIA_VI_VPSS_Mode_Init(void)
{
	MAPI_MEDIA_SYS_ATTR_T sys_attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetVbParam(&sys_attr);

	s32Ret = MAPI_SYS_VI_VPSS_Mode_Init(&sys_attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VI_VPSS_Mode_Init fail");

	return 0;
}

int32_t MEDIA_VbInitPlayBack(void)
{
	MAPI_MEDIA_SYS_ATTR_T sys_attr = {0};
	PARAM_DISP_ATTR_S disp_attr;
	int32_t s32Ret = 0;

	PARAM_GetVbParam(&sys_attr);
	PARAM_GetVoParam(&disp_attr);

	if (disp_attr.Rotate == 0) {
		sys_attr.vb_pool[0].vb_blk_size.frame.width = disp_attr.Width;
		sys_attr.vb_pool[0].vb_blk_size.frame.height = disp_attr.Height;
	} else {
		sys_attr.vb_pool[0].vb_blk_size.frame.width = disp_attr.Height;
		sys_attr.vb_pool[0].vb_blk_size.frame.height = disp_attr.Width;
	}
	sys_attr.vb_pool_num = 1;
	sys_attr.vb_pool[0].is_frame = true;
	sys_attr.vb_pool[0].vb_blk_size.frame.fmt = 13;
	sys_attr.vb_pool[0].vb_blk_num = 5;
	// set offline
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		sys_attr.stVIVPSSMode.aenMode[i] = VI_OFFLINE_VPSS_OFFLINE;
	}
	sys_attr.stVPSSMode.aenInput[0] = VPSS_INPUT_MEM;
	sys_attr.stVPSSMode.aenInput[1] = VPSS_INPUT_MEM;
	s32Ret = MAPI_Media_Init(&sys_attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_Media_Init fail");

	return 0;
}

int32_t MEDIA_VIVPSSInitPlayBack(void)
{
	MAPI_MEDIA_SYS_ATTR_T sys_attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetVbParam(&sys_attr);
	// set offline
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		sys_attr.stVIVPSSMode.aenMode[i] = VI_OFFLINE_VPSS_OFFLINE;
	}
	sys_attr.stVPSSMode.aenInput[0] = VPSS_INPUT_MEM;
	sys_attr.stVPSSMode.aenInput[1] = VPSS_INPUT_MEM;
	s32Ret = MAPI_SYS_VI_VPSS_Mode_Init(&sys_attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_SYS_VI_VPSS_Mode_Init fail");

	return 0;
}

int32_t MEDIA_VbDeInit(void)
{
	int32_t s32Ret = 0;

	s32Ret = MAPI_Media_Deinit();
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_Media_Deinit fail");

	return 0;
}

int32_t MEDIA_DispInit(bool windowMode)
{
	int32_t ret = MAPI_SUCCESS;
#ifdef SCREEN_ON
	PARAM_DISP_ATTR_S params;
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	PARAM_GetVoParam(&params);

	if (Syshdl->dispHdl == NULL) {
		MEDIA_DispCfg dispCfg;
		dispCfg.dispAttr.width = params.Width;
		dispCfg.dispAttr.height = params.Height;
		dispCfg.dispAttr.rotate = params.Rotate;
		dispCfg.dispAttr.fps = params.Fps;
		dispCfg.dispAttr.window_mode = windowMode;
		dispCfg.dispAttr.stPubAttr.u32BgColor = COLOR_10_RGB_BLACK;
		dispCfg.dispAttr.stPubAttr.enIntfSync = params.EnIntfSync;
		dispCfg.videoLayerAttr.u32BufLen = 3;
		// TODO cv182x
		dispCfg.videoLayerAttr.u32PixelFmt = params.frame_fmt;

#ifdef ENABLE_VO_INIT
		(HAL_SCREEN_COMM_Register(HAL_SCREEN_IDXS_0, &stHALSCREENObj));

		PARAM_PWM_S Param;
		s32Ret = PARAM_GetPWMParam(&Param);
		if (s32Ret == 0) {
			HAL_SCREEN_COMM_SetLuma(HAL_SCREEN_IDXS_0, Param.PWMCfg);
		} else {
			CVI_LOGE("%s : PARAM_GetPWMParam failed\n", __func__);
		}
		HAL_SCREEN_COMM_SetBackLightState(HAL_SCREEN_IDXS_0, HAL_SCREEN_STATE_ON);

		CVI_LOGD("init panel app ======================");
		// CHECK_RET(HAL_SCREEN_Init(HAL_SCREEN_IDXS_0));
		HAL_SCREEN_ATTR_S screenAttr = {0};
		HAL_SCREEN_COMM_GetAttr(HAL_SCREEN_IDXS_0, &screenAttr);
		switch (screenAttr.enType) {
		case HAL_COMP_SCREEN_INTF_TYPE_MIPI:
			dispCfg.dispAttr.stPubAttr.enIntfType = VO_INTF_MIPI;
			break;
		case HAL_COMP_SCREEN_INTF_TYPE_LCD:
			// dispCfg.dispAttr.stPubAttr.enIntfType = VO_INTF_LCD;
			// dispCfg.dispAttr.stPubAttr.stLcdCfg = *(VO_LCD_CFG_S *)&screenAttr.unScreenAttr.stLcdAttr.stLcdCfg;
			break;
		case HAL_COMP_SCREEN_INTF_TYPE_I80:
			#ifdef CHIP_184X
			dispCfg.dispAttr.stPubAttr.enIntfType = VO_INTF_I80;
			#else
			dispCfg.dispAttr.stPubAttr.enIntfType = VO_INTF_I80_HW;
			#endif
			break;
		default:
			CVI_LOGD("Invalid screen type\n");
			return MAPI_ERR_FAILURE;
		}

		dispCfg.dispAttr.stPubAttr.stSyncInfo.bSynm = 1; /**<sync mode: signal */
		dispCfg.dispAttr.stPubAttr.stSyncInfo.bIop = 1;	 /**<progressive display */
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16FrameRate = screenAttr.stAttr.u32Framerate;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Vact = screenAttr.stAttr.stSynAttr.u16Vact;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Vbb = screenAttr.stAttr.stSynAttr.u16Vbb;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Vfb = screenAttr.stAttr.stSynAttr.u16Vfb;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Hact = screenAttr.stAttr.stSynAttr.u16Hact;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Hbb = screenAttr.stAttr.stSynAttr.u16Hbb;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Hfb = screenAttr.stAttr.stSynAttr.u16Hfb;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Hpw = screenAttr.stAttr.stSynAttr.u16Hpw;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.u16Vpw = screenAttr.stAttr.stSynAttr.u16Vpw;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.bIdv = screenAttr.stAttr.stSynAttr.bIdv;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.bIhs = screenAttr.stAttr.stSynAttr.bIhs;
		dispCfg.dispAttr.stPubAttr.stSyncInfo.bIvs = screenAttr.stAttr.stSynAttr.bIvs;

		dispCfg.videoLayerAttr.u32VLFrameRate = screenAttr.stAttr.u32Framerate;
		dispCfg.videoLayerAttr.stImageSize.u32Width = screenAttr.stAttr.u32Width;
		dispCfg.videoLayerAttr.stImageSize.u32Height = screenAttr.stAttr.u32Height;

#endif
		dispCfg.dispAttr.pixel_format = dispCfg.videoLayerAttr.u32PixelFmt;

		s32Ret = MAPI_DISP_Init(&dispCfg.dispHdl, 0, &dispCfg.dispAttr);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_DISP_Init fail");

		s32Ret = MAPI_DISP_Start(dispCfg.dispHdl, &dispCfg.videoLayerAttr);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_DISP_Start fail");
		Syshdl->dispHdl = dispCfg.dispHdl;
	}
#endif
	return ret;
}

int32_t MEDIA_DispDeInit(void)
{
#ifdef SCREEN_ON
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	int32_t s32Ret = 0;

	s32Ret = MAPI_DISP_Stop(Syshdl->dispHdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_DISP_Stop fail");

	s32Ret = MAPI_DISP_Deinit(Syshdl->dispHdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_DISP_Deinit fail");
	Syshdl->dispHdl = NULL;
#endif
	return 0;
}

#ifdef SERVICES_LIVEVIEW_ON
int32_t MEDIA_LiveViewSerInit(void)
{
#ifdef SCREEN_ON
	uint32_t i = 0, j = 0;
	int32_t s32Ret = 0;
	LIVEVIEW_SERVICE_HANDLE_T *plvHdl = &MEDIA_GetCtx()->SysServices.LvHdl;
	LIVEVIEW_SERVICE_PARAM_S *plvParams = &MEDIA_GetCtx()->SysServices.LvParams;
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	PARAM_WND_ATTR_S *WndParam = &PARAM_GetCtx()->pstCfg->MediaComm.Window;

	uint32_t wndNum = 0;
	for (i = 0; i < WndParam->WndCnt; i++) {
		WndParam->Wnds[i].SmallWndEnable = false;
		if (WndParam->Wnds[i].WndEnable == false) {
			continue;
		}
		wndNum++;
	}

	plvParams->WndCnt = WndParam->WndCnt;
	for (i = 0; i < WndParam->WndCnt; i++) {
		if (wndNum > 1) {
			WndParam->Wnds[i].SmallWndEnable = true;
		}
		memcpy(&plvParams->LiveviewService[i].wnd_attr, &WndParam->Wnds[i], sizeof(LIVEVIEW_SERVICE_WNDATTR_S));
		for (j = 0; j < MAX_VPROC_CNT; j++) {
			if ((SysHandle->vproc[j] != NULL) && (WndParam->Wnds[i].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(SysHandle->vproc[j]))) {
				plvParams->LiveviewService[i].vproc_hdl = SysHandle->vproc[j];
			}
		}
	}

	s32Ret = LIVEVIEW_SERVICE_Create(plvHdl, plvParams);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "LIVEVIEW_SERVICE_Create fail");
#endif
	return 0;
}

int32_t MEDIA_LiveViewSerDeInit()
{
#ifdef SCREEN_ON
	int32_t s32Ret = 0;

	s32Ret = LIVEVIEW_SERVICE_Destroy(MEDIA_GetCtx()->SysServices.LvHdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "LIVEVIEW_SERVICE_Destroy fail");
#endif
	return 0;
}
#endif

int32_t MEDIA_AiInit(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	MAPI_ACAP_ATTR_S attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetAiParam(&attr);

	s32Ret = MAPI_ACAP_Init(&SysHandle->aihdl, &attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_ACAP_Init fail");

	s32Ret = MAPI_ACAP_Start(SysHandle->aihdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_ACAP_Start fail");

	return 0;
}

int32_t MEDIA_AiDeInit(void)
{
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;

	s32Ret = MAPI_ACAP_Deinit(SysHandle->aihdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_ACAP_Deinit fail");

	return 0;
}

int32_t MEDIA_AencInit(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	MAPI_AENC_ATTR_S attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetAencParam(&attr);
	if (attr.enAencFormat >= MAPI_AUDIO_CODEC_BUTT) {
		return 0;
	}

	s32Ret = MAPI_AENC_Init(&SysHandle->aenchdl, &attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AENC_Init fail");

	s32Ret = MAPI_AENC_Start(SysHandle->aenchdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AENC_Start fail");

	s32Ret = MAPI_AENC_BindACap(0, 0, 0, 0);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AENC_BindACap failed");

	return 0;
}

int32_t MEDIA_AencDeInit(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	MAPI_AENC_ATTR_S attr = {0};
	int32_t s32Ret = 0;

	PARAM_GetAencParam(&attr);
	if (attr.enAencFormat >= MAPI_AUDIO_CODEC_BUTT) {
		return 0;
	}

	s32Ret = MAPI_AENC_Deinit(SysHandle->aenchdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AENC_Deinit fail");

	return 0;
}

int32_t MEDIA_AoInit(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	MAPI_AO_ATTR_S ao_attr;
	int32_t s32Ret = 0;

	PARAM_GetAoParam(&ao_attr);

	ao_attr.u32PowerPinId = ao_attr.u32PowerPinId;
	s32Ret = MAPI_AO_Init(&SysHandle->aohdl, &ao_attr);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AO_Init fail");

	return 0;
}

int32_t MEDIA_PlayBootSound(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	int32_t s32Ret = 0;

	MEDIA_PARAM_INIT_S *PSysMediaParams = MEDIA_GetCtx();
	if (PSysMediaParams->bInited == false) {
		PSysMediaParams->bInited = true;
		s32Ret = SYSTEM_BootSound(SysHandle->aohdl);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "SYSTEM_BootSound fail");
	}

	return 0;
}

int32_t MEDIA_AoDeInit(void)
{
	MEDIA_SYSHANDLE_S *SysHandle = &MEDIA_GetCtx()->SysHandle;
	int32_t s32Ret = 0;

	s32Ret = MAPI_AO_Deinit(SysHandle->aohdl);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_AO_Deinit fail");

	return 0;
}

#ifdef SERVICES_RTSP_ON

static void rtsp_play(int32_t references, void *arg)
{
    RTSP_VENC_CTX *ctx = (RTSP_VENC_CTX *)arg;
    int32_t sns_id = ctx->param->rtsp_id;
    MEDIA_PARAM_INIT_S *media_params = MEDIA_GetCtx();
    RTSP_SERVICE_HANDLE_T hdl = media_params->SysServices.RtspHdl[sns_id];
	pthread_mutex_lock(&ctx->mutex);
    int32_t vpss_grp = -1;
    int32_t chnid = ctx->param->chn_id;
    if (MAPI_VPROC_IsExtChn(ctx->param->vproc, chnid)) {
        vpss_grp = MAPI_VPROC_GetExtChnGrp(ctx->param->vproc, chnid);
        if (vpss_grp < 0) {
            CVI_LOGE("invalid group for ext chn %d", chnid);
            pthread_mutex_unlock(&ctx->mutex);
            return;
        }
        chnid = 0;
    } else {
        vpss_grp = MAPI_VPROC_GetGrp(ctx->param->vproc);
        if (vpss_grp < 0) {
            CVI_LOGE("invalid group");
            pthread_mutex_unlock(&ctx->mutex);
            return;
        }
    }
	#ifndef SERVICES_SUBVIDEO_ON
    if (references == 0 && ctx->param->venc_hdl == NULL) {
        #ifdef ENABLE_ISP_PQ_TOOL
        ctx->venc_attr.venc_param.bitrate_kbps = 20000;
        ctx->param->bitrate_kbps = ctx->venc_attr.venc_param.bitrate_kbps;
        #endif
        MAPI_VENC_InitChn(&ctx->param->venc_hdl, &ctx->venc_attr);
        if(ctx->param->venc_hdl){
            // if(ctx->venc_attr.venc_param.codec == MAPI_VCODEC_H264
            // || ctx->venc_attr.venc_param.codec == MAPI_VCODEC_H265){
            //     MAPI_VENC_SetDataFifoLen(ctx->param->venc_hdl, ctx->venc_attr.venc_param.datafifoLen);
            //     uint32_t datafifoLen = 0;
            //     MAPI_VENC_GetDataFifoLen(ctx->param->venc_hdl, &datafifoLen);
            //     CVI_LOGD("datafifoLen %u", datafifoLen);
            // }
            MAPI_VENC_BindVproc(ctx->param->venc_hdl, vpss_grp, chnid);
            MAPI_VENC_StartRecvFrame(ctx->param->venc_hdl, -1);
            RTSP_SERVICE_UpdateParam(hdl, ctx->param);
        }
    }
	#else
    ctx->param->venc_hdl = media_params->SysHandle.venchdl[vpss_grp][chnid];
    RTSP_SERVICE_UpdateParam(hdl, ctx->param);
    #endif

    pthread_mutex_unlock(&ctx->mutex);
}

static void rtsp_teardown(int32_t references, void *arg)
{
	RTSP_VENC_CTX *ctx = (RTSP_VENC_CTX *)arg;
	int32_t sns_id = ctx->param->rtsp_id;
	MEDIA_PARAM_INIT_S *media_params = MEDIA_GetCtx();
	RTSP_SERVICE_HANDLE_T rtspHdl = media_params->SysServices.RtspHdl[sns_id];
	pthread_mutex_lock(&ctx->mutex);
	#ifndef SERVICES_SUBVIDEO_ON
	int32_t vpss_grp = -1;
	int32_t chnid = ctx->param->chn_id;
	if (MAPI_VPROC_IsExtChn(ctx->param->vproc, chnid)) {
		vpss_grp = MAPI_VPROC_GetExtChnGrp(ctx->param->vproc, chnid);
		if (vpss_grp < 0) {
			CVI_LOGE("invalid group for ext chn %d", chnid);
			pthread_mutex_unlock(&ctx->mutex);
			return;
		}
		chnid = 0;
	} else {
		vpss_grp = MAPI_VPROC_GetGrp(ctx->param->vproc);
		if (vpss_grp < 0) {
			CVI_LOGE("invalid group");
			pthread_mutex_unlock(&ctx->mutex);
			return;
		}
	}
	if (references == 0 && ctx->param->venc_hdl != NULL) {
		MAPI_VENC_HANDLE_T hdl = ctx->param->venc_hdl;
		ctx->param->venc_hdl = NULL;
		RTSP_SERVICE_UpdateParam(rtspHdl, ctx->param);

		MAPI_VENC_UnBindVproc(hdl, vpss_grp, chnid);
		MAPI_VENC_StopRecvFrame(hdl);
		MAPI_VENC_DeinitChn(hdl);
	}
	#else
    ctx->param->venc_hdl = NULL;
    RTSP_SERVICE_UpdateParam(rtspHdl, ctx->param);
    #endif
	pthread_mutex_unlock(&ctx->mutex);
}

int32_t MEDIA_RtspSerInit(void)
{
	int32_t i = 0, j = 0, z = 0;
	int32_t s32Ret = 0;
	PARAM_MEDIA_COMM_S mediacomm;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_GetMediaComm(&mediacomm);
	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		RTSP_SERVICE_HANDLE_T *rtspHdl = &MediaParams->SysServices.RtspHdl[i];
		RTSP_SERVICE_PARAM_S *rtspParam = &MediaParams->SysServices.RtspParams[i];
		PARAM_RTSP_CHN_ATTR_S *rtspattr = &mediacomm.Rtsp.ChnAttrs[i];
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);
		if (rtspattr->Enable == true) {
			rtspParam->rtsp_id = i;
			rtspParam->acap_hdl = MediaParams->SysHandle.aihdl;
			rtspParam->aenc_hdl = MediaParams->SysHandle.aenchdl;
			rtspParam->audio_codec = RTSP_AUDIO_CODEC_PCM;
			rtspParam->audio_sample_rate = mediacomm.Ai.enSampleRate;
			if (mediacomm.Ai.bVqeOn == 1) {
				rtspParam->audio_channels = 1;
			} else {
				rtspParam->audio_channels = mediacomm.Ai.AudioChannel;
			}
			rtspParam->audio_pernum = mediacomm.Ai.u32PtNumPerFrm;
			rtspParam->max_conn = rtspattr->MaxConn;
			rtspParam->timeout = rtspattr->Timeout;
			rtspParam->port = rtspattr->Port;
#ifdef NETPROTOCOL_CGI
			snprintf(rtspParam->rtsp_name, sizeof(rtspParam->rtsp_name), "liveRTSP/av%d", i + 4);
#else
			strncpy(rtspParam->rtsp_name, rtspattr->Name, sizeof(rtspParam->rtsp_name) - 1);
#endif

			for (j = 0; j < MAX_VENC_CNT; j++) {
				// ENABLE
				if (params.VencAttr.VencChnAttr[j].VencChnEnable == true) {
					if (rtspattr->Enable == true && params.VencAttr.VencChnAttr[j].VencId == rtspattr->BindVencId) {
						rtspParam->video_codec = params.VencAttr.VencChnAttr[j].MapiVencAttr.codec;
						rtspParam->width = params.VencAttr.VencChnAttr[j].MapiVencAttr.width;
						rtspParam->height = params.VencAttr.VencChnAttr[j].MapiVencAttr.height;
						rtspParam->bitrate_kbps = params.VencAttr.VencChnAttr[j].MapiVencAttr.bitrate_kbps;
						rtspParam->chn_id = params.VencAttr.VencChnAttr[j].BindVprocChnId;
						rtspParam->framerate = params.VencAttr.VencChnAttr[j].framerate;
						for (z = 0; z < MAX_VPROC_CNT; z++) {
							if ((MediaParams->SysHandle.vproc[z] != NULL) &&
								(params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
								rtspParam->vproc = MediaParams->SysHandle.vproc[z];
								break;
							}
						}
					}
				}
			}
			rtsp_venc_ctx[i].param = rtspParam;
			pthread_mutex_init(&rtsp_venc_ctx[i].mutex, NULL);
			rtspParam->rtsp_play = rtsp_play;
			rtspParam->rtsp_play_arg = (void *)&rtsp_venc_ctx[i];
			rtspParam->rtsp_teardown = rtsp_teardown;
			rtspParam->rtsp_teardown_arg = (void *)&rtsp_venc_ctx[i];
			s32Ret = RTSP_SERVICE_Create(rtspHdl, rtspParam);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_RtspSerInit fail");
		}
	}

	return 0;
}

int32_t MEDIA_RtspSerDeInit(void)
{
	int32_t s32Ret = 0;
	for (int32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		RTSP_SERVICE_HANDLE_T *rtspHdl = &MEDIA_GetCtx()->SysServices.RtspHdl[i];
		if (*rtspHdl != NULL) {
			s32Ret = RTSP_SERVICE_Destroy(*rtspHdl);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "RTSP_SERVICE_Destroy fail");
			*rtspHdl = NULL;
			pthread_mutex_destroy(&rtsp_venc_ctx[i].mutex);
		}
	}

	return 0;
}

int32_t MEDIA_APP_RTSP_Init(uint32_t id, char *name)
{
	int32_t s32Ret = 0;
	PARAM_MEDIA_COMM_S mediacomm;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_GetMediaComm(&mediacomm);
	RTSP_SERVICE_HANDLE_T *rtspHdl = &MediaParams->SysServices.RtspHdl[id];
	if (*rtspHdl == NULL) {
		RTSP_SERVICE_PARAM_S *rtspParam = &MediaParams->SysServices.RtspParams[id];
		memcpy(rtspParam, &MediaParams->SysServices.RtspParams[0], sizeof(RTSP_SERVICE_PARAM_S));
		rtspParam->rtsp_id = id;
		strncpy(rtspParam->rtsp_name, name, strlen(name));

		rtsp_venc_ctx[id] = rtsp_venc_ctx[0];
		rtsp_venc_ctx[id].param = rtspParam;
		pthread_mutex_init(&rtsp_venc_ctx[id].mutex, NULL);
		rtspParam->rtsp_play_arg = (void *)&rtsp_venc_ctx[id];
		rtspParam->rtsp_teardown_arg = (void *)&rtsp_venc_ctx[id];
		s32Ret = RTSP_SERVICE_Create(rtspHdl, rtspParam);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_APP_RTSP_Init fail");
		usleep(200 * 1000);
	}
	return 0;
}

int32_t MEDIA_SwitchRTSPChanel(uint32_t value, uint32_t id, char *name)
{
	RTSP_SERVICE_StartStop(0, name); // 关闭直播流
	MEDIA_PARAM_INIT_S *media_params = MEDIA_GetCtx();
	RTSP_SERVICE_HANDLE_T hdl = media_params->SysServices.RtspHdl[id];
	RTSP_SERVICE_PARAM_S *rtspParam = &media_params->SysServices.RtspParams[id];
	rtspParam->vproc = media_params->SysHandle.vproc[value]; // 切换成设定通道
	RTSP_SERVICE_UpdateParam(hdl, rtspParam);
	RTSP_SERVICE_StartStop(1, name); // 开启直播流
	return 0;
}

int32_t MEDIA_APP_RTSP_DeInit()
{
	int32_t s32Ret = 0;
	for (int32_t i = MAX_CAMERA_INSTANCES; i < MAX_RTSP_CNT; i++) {
		RTSP_SERVICE_HANDLE_T *rtspHdl = &MEDIA_GetCtx()->SysServices.RtspHdl[i];
		if (*rtspHdl != NULL) {
			s32Ret = RTSP_SERVICE_Destroy(*rtspHdl);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "RTSP_SERVICE_Destroy fail");
			*rtspHdl = NULL;
			pthread_mutex_destroy(&rtsp_venc_ctx[i].mutex);
		}
	}
	return 0;
}
#endif

static int32_t get_audio_codec_type(int32_t file_type)
{
	if ((file_type == RECORD_SERVICE_FILE_TYPE_TS) || (file_type == RECORD_SERVICE_FILE_TYPE_MP4)) {
		return RECORD_SERVICE_AUDIO_CODEC_AAC;
	} else if (RECORD_SERVICE_FILE_TYPE_ES == file_type) {
		return RECORD_SERVICE_AUDIO_CODEC_NONE;
	}

	return RECORD_SERVICE_AUDIO_CODEC_PCM;
}

static int32_t RECORD_GetSubtitleCallBack(void *p, int32_t viPipe, char *str, int32_t str_len)
{
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	int32_t status = 0;
	MAPI_VCAP_GetSensorPipeAttr(Syshdl->sns[viPipe], &status);
	if (0 == status) {
		// CVI_LOGD("stViPipeAttr.bYuvBypassPath is true, yuv sensor skip isp ops");
		return -1;
	}

	ISP_EXP_INFO_S expInfo;
	ISP_WB_INFO_S wbInfo;
	memset(&expInfo, 0, sizeof(ISP_EXP_INFO_S));
	memset(&wbInfo, 0, sizeof(ISP_WB_INFO_S));
	CVI_ISP_QueryExposureInfo(viPipe, &expInfo);
	CVI_ISP_QueryWBInfo(viPipe, &wbInfo);
	snprintf(str, str_len, "#AE ExpT:%u SExpT:%u LExpT:%u AG:%u DG:%u IG:%u Exp:%u ExpIsMax:%d AveLum:%d PIrisFno:%d Fps:%u ISO:%u #AWB RG:%d BG:%d CT:%d",
			 expInfo.u32ExpTime, expInfo.u32ShortExpTime, expInfo.u32LongExpTime, expInfo.u32AGain,
			 expInfo.u32DGain, expInfo.u32ISPDGain, expInfo.u32Exposure, expInfo.bExposureIsMAX,
			 expInfo.u8AveLum, expInfo.u32PirisFNO, expInfo.u32Fps, expInfo.u32ISO,
			 wbInfo.u16Rgain, wbInfo.u16Bgain, wbInfo.u16ColorTemp);

	return 0;
}

#ifdef SERVICES_SUBVIDEO_ON
int32_t MEDIA_StartVideoInTask(void)
{
    int s32Ret = 0;
    MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
    PARAM_MEDIA_COMM_S mediacomm;
    PARAM_GetMediaComm(&mediacomm);
    int i,j;
    for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
    #ifdef RESET_MODE_AHD_HOTPLUG_ON
        if (MEDIA_Is_CameraEnabled(i) == false) {
            continue;
        }
    #endif
        PARAM_MEDIA_SPEC_S params;
        memset(&params, 0x0, sizeof(PARAM_MEDIA_SPEC_S));
        PARAM_GetMediaMode(i, &params);
        PARAM_RECORD_CHN_ATTR_S     *recattr = &mediacomm.Record.ChnAttrs[i];
        for (j = 0; j < MAX_VENC_CNT; j++)
        {
            if (params.VencAttr.VencChnAttr[j].VencChnEnable == true)
            {
                if (recattr->Enable == true && recattr->Subvideoen == true && params.VencAttr.VencChnAttr[j].VencId == recattr->SubBindVencId) {
                    s32Ret |= VIDEO_SERVICR_TaskStart(i,MediaParams->SysHandle.venchdl[i][j],params.VencAttr.VencChnAttr[j].BindVprocId,params.VencAttr.VencChnAttr[j].BindVprocChnId);
                }
            }
        }

    }
    return s32Ret;
}

int32_t MEDIA_StopVideoInTask(void)
{
    int s32Ret = 0;
    PARAM_MEDIA_COMM_S mediacomm;
    PARAM_GetMediaComm(&mediacomm);
    for (int i = 0; i < MAX_CAMERA_INSTANCES; i++) {
    #ifdef RESET_MODE_AHD_HOTPLUG_ON
        if (MEDIA_Is_CameraEnabled(i) == false) {
            continue;
        }
    #endif
        PARAM_RECORD_CHN_ATTR_S     *recattr = &mediacomm.Record.ChnAttrs[i];
        if (recattr->Enable == true && recattr->Subvideoen == true){
            s32Ret |= VIDEO_SERVICR_TaskStop(i);
        }
    }
    return s32Ret;
}
#endif

int32_t MEDIA_StartAudioInTask(void)
{
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	return AUDIO_SERVICR_ACAP_TaskStart(MediaParams->SysHandle.aihdl, MediaParams->SysHandle.aenchdl);
}

int32_t MEDIA_StopAudioInTask(void)
{
	return AUDIO_SERVICR_ACAP_TaskStop();
}

#ifdef GPS_ON
static int32_t MEDIA_GPSCallBack(GPSMNG_MSG_PACKET *msgPacket, void *privateData)
{
	(void)privateData;
	RECORD_SERVICE_GPS_INFO_S gps_info;
	if (msgPacket) {
		memset(&gps_info, 0x0, sizeof(RECORD_SERVICE_GPS_INFO_S));
		gps_info.rmc_info.Hour = msgPacket->gpsRMC.utc.hour;
		gps_info.rmc_info.Minute = msgPacket->gpsRMC.utc.min;
		gps_info.rmc_info.Second = msgPacket->gpsRMC.utc.sec;
		gps_info.rmc_info.Year = msgPacket->gpsRMC.utc.year;
		gps_info.rmc_info.Month = msgPacket->gpsRMC.utc.mon;
		gps_info.rmc_info.Day = msgPacket->gpsRMC.utc.day;
		gps_info.rmc_info.Status = msgPacket->gpsRMC.status;
		gps_info.rmc_info.NSInd = msgPacket->gpsRMC.ns;
		gps_info.rmc_info.EWInd = msgPacket->gpsRMC.ew;
		gps_info.rmc_info.reserved = 'A';
		gps_info.rmc_info.Latitude = msgPacket->gpsRMC.lat;
		gps_info.rmc_info.Longitude = msgPacket->gpsRMC.lon;
		gps_info.rmc_info.Speed = msgPacket->gpsRMC.speed;
		gps_info.rmc_info.Angle = msgPacket->gpsRMC.direction;
		// gps_info.rmc_info.ID[0] = 'G';
		strcpy(gps_info.rmc_info.ID, "1A9464FF740E215FASXT");
		gps_info.rmc_info.GsensorX = 10;
		gps_info.rmc_info.GsensorY = 20;
		gps_info.rmc_info.GsensorZ = 30;

		pthread_mutex_lock(&gGPSMutex);
		memcpy(&gstGPSInfo, &gps_info, sizeof(RECORD_SERVICE_GPS_INFO_S));
		pthread_mutex_unlock(&gGPSMutex);
		return 0;
	}
	return -1;
}

static int32_t MEDIA_GetGPSInfo(RECORD_SERVICE_GPS_INFO_S *info)
{
	pthread_mutex_lock(&gGPSMutex);
	memcpy(info, &gstGPSInfo, sizeof(RECORD_SERVICE_GPS_INFO_S));
	pthread_mutex_unlock(&gGPSMutex);
	return 0;
}
#endif

#ifdef SERVICES_PHOTO_ON
int32_t MEDIA_PhotoVprocDeInit(void)
{
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	s32Ret = MAPI_VPROC_DisableTileMode();
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MAPI_VPROC_DisableTileMode fail");

	s32Ret = MAPI_VPROC_Deinit(Syshdl->vproc[MAX_VPROC_CNT - 1]);
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "MEDIA_PhotoVprocDeInit fail");
	Syshdl->vproc[MAX_VPROC_CNT - 1] = NULL;

	return s32Ret;
}

int32_t MEDIA_PhotoVprocInit(void)
{
	uint32_t Photowidth = 0;
	uint32_t Photoheight = 0;
	PARAM_MEDIA_COMM_S mediaparams;
	PARAM_GetMediaComm(&mediaparams);
	PARAM_PHOTO_ATTR_S *Photo_attr = &mediaparams.Photo;
	PARAM_MEDIA_SPEC_S params;

	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PARAM_GetMediaMode(i, &params);
		for (int32_t j = 0; j < MAX_VENC_CNT; j++) {
			if (params.VencAttr.VencChnAttr[j].VencId == Photo_attr->ChnAttrs[i].BindVencId) {
				if (params.VencAttr.VencChnAttr[j].MapiVencAttr.width > Photowidth) {
					Photowidth = params.VencAttr.VencChnAttr[j].MapiVencAttr.width;
					Photoheight = params.VencAttr.VencChnAttr[j].MapiVencAttr.height;
				}
				break;
			}
		}
	}

	int32_t s32Ret = 0;

	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;

	MAPI_VPROC_ATTR_T g_vproc_attr = {0};
	g_vproc_attr = MAPI_VPROC_DefaultAttr_OneChn(
		3840, 2160, PIXEL_FORMAT_NV12,
		Photowidth, Photoheight, PIXEL_FORMAT_NV12);

	g_vproc_attr.attr_chn[0].stAspectRatio.enMode = ASPECT_RATIO_AUTO;
	g_vproc_attr.attr_chn[0].stAspectRatio.stVideoRect.s32X = 0;
	g_vproc_attr.attr_chn[0].stAspectRatio.stVideoRect.s32Y = 0;
	g_vproc_attr.attr_chn[0].stAspectRatio.stVideoRect.u32Width = Photowidth;
	g_vproc_attr.attr_chn[0].stAspectRatio.stVideoRect.u32Height = Photoheight;
	g_vproc_attr.attr_chn[0].stAspectRatio.bEnableBgColor = CVI_TRUE;
	g_vproc_attr.attr_chn[0].stAspectRatio.u32BgColor = RGB_8BIT(0, 0, 0);
	g_vproc_attr.attr_inp.u8VpssDev = 1;
	g_vproc_attr.chn_vbcnt[0] = 0;
	g_vproc_attr.chn_bufWrap[0].bEnable = CVI_TRUE;
	g_vproc_attr.chn_bufWrap[0].u32BufLine = 64;
	g_vproc_attr.chn_bufWrap[0].u32WrapBufferSize = 5;

	s32Ret = MAPI_VPROC_Init(&Syshdl->vproc[MAX_VPROC_CNT - 1], -1, &g_vproc_attr);
	if (s32Ret != MAPI_SUCCESS) {
		CVI_LOGE("MAPI_VPROC_Init Photo failed\n");
		return s32Ret;
	}
	CVI_LOGI("Window mode, g_vproc_attr photo Created with VPSS grp %d\n", MAPI_VPROC_GetGrp(Syshdl->vproc[MAX_VPROC_CNT - 1]));

	s32Ret = MAPI_VPROC_EnableTileMode();
	if (s32Ret != MAPI_SUCCESS) {
		CVI_LOGE("MAPI_VPROC_EanbleTileMode Photo failed\n");
		return s32Ret;
	}

	return 0;
}

int32_t MEDIA_PhotoSerInit(void)
{
	int32_t i = 0, j = 0, z = 0;
	int32_t s32Ret = 0;
	uint32_t menu_index = 0;
	PARAM_MENU_S menu_param = {0};
	PARAM_MEDIA_COMM_S mediacomm;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_DEVMNG_S devmng = {0};
	PARAM_FILEMNG_S FileMng;
	PARAM_GetMenuParam(&menu_param);
	PARAM_GetMediaComm(&mediacomm);
	PARAM_GetFileMngParam(&FileMng);
	PARAM_GetDevParam(&devmng);
	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PHOTO_SERVICE_HANDLE_T *photoSerhdl = &MediaParams->SysServices.PhotoHdl[i];
		PHOTO_SERVICE_PARAM_S *param = &MediaParams->SysServices.PhotoParams[i];
		PARAM_PHOTO_ATTR_S *photoattr = &mediacomm.Photo;
		PARAM_THUMBNAIL_CHN_ATTR_S *thumbnail_attr = &mediacomm.Thumbnail.ChnAttrs[i];
		PARAM_SUB_PIC_CHN_ATTR_S *sub_pic_attr = &mediacomm.SubPic.ChnAttrs[i];
		uint32_t prealloclen = FileMng.FileMng.dir_param[i].prealloc_sizeMB[FILEMNG_DIR_PHOTO];
		PARAM_MEDIA_SPEC_S params;

		param->photo_id = i;
		param->cont_photo_event_cb = PHOTOMNG_ContCallBack;
		param->prealloclen = prealloclen;
		param->enable_dump_raw = photoattr->ChnAttrs[i].EnableDumpRaw;
		param->scale_vproc = MediaParams->SysHandle.vproc[MAX_VPROC_CNT - 1];
		param->scale_vproc_chn_id = 0;
		if(photoattr->ChnAttrs[i].BindVcapId == (uint32_t)i) {
			param->src_vcap = MediaParams->SysHandle.sns[i];
		}
		param->flash_led_gpio = devmng.FlashLed.GpioNum;
		param->flash_led_pulse = devmng.FlashLed.Pulse;
		param->flash_led_thres = devmng.FlashLed.Thres;
		for (int32_t n = 0; n < MAX_CAMERA_INSTANCES; n++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
			if (MEDIA_Is_CameraEnabled(i) == false) {
				continue;
			}
#endif
			PARAM_GetMediaMode(n, &params);
			for (j = 0; j < MAX_VENC_CNT; j++) {
				if (photoattr->ChnAttrs[i].Enable && params.VencAttr.VencChnAttr[j].VencChnEnable == true) {
					if (params.VencAttr.VencChnAttr[j].VencId == photoattr->ChnAttrs[i].BindVencId) {
						param->src_vproc_chn_id = params.VencAttr.VencChnAttr[j].BindVprocChnId;
						param->photo_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
						param->photo_bufsize = params.VencAttr.VencChnAttr[j].MapiVencAttr.bufSize;

						for (z = 0; z < MAX_VPROC_CNT; z++) {
							if ((MediaParams->SysHandle.vproc[z] != NULL)) {
								if ((params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
									param->src_vproc = MediaParams->SysHandle.vproc[z];
									break;
								}
							}
						}
					}

					if (params.VencAttr.VencChnAttr[j].VencId == thumbnail_attr->BindVencId) {
						param->thumbnail_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
						param->vproc_chn_id_thumbnail = params.VencAttr.VencChnAttr[j].BindVprocChnId;
						param->thumbnail_bufsize = params.VencAttr.VencChnAttr[j].MapiVencAttr.bufSize;
						for (z = 0; z < MAX_VPROC_CNT; z++) {
							if ((MediaParams->SysHandle.vproc[z] != NULL) &&
								(params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
								param->thumbnail_vproc = MediaParams->SysHandle.vproc[z];
								break;
							}
						}
					}

					if (params.VencAttr.VencChnAttr[j].VencId == sub_pic_attr->BindVencId) {
						param->sub_pic_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
						param->vproc_chn_id_sub_pic = params.VencAttr.VencChnAttr[j].BindVprocChnId;
						param->sub_pic_bufsize = params.VencAttr.VencChnAttr[j].MapiVencAttr.bufSize;
						for (z = 0; z < MAX_VPROC_CNT; z++) {
							if ((MediaParams->SysHandle.vproc[z] != NULL) &&
								(params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
								param->sub_pic_vproc = MediaParams->SysHandle.vproc[z];
								break;
							}
						}
					}
				}
			}
		}
		s32Ret = PHOTO_SERVICE_Create(photoSerhdl, param);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "PHOTO_SERVICE_Create fail");

		menu_index = menu_param.PhotoQuality.Current;
		s32Ret = PHOTO_SERVICE_SetQuality(*photoSerhdl, menu_param.PhotoQuality.Items[menu_index].Value);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "PHOTO_SERVICE_SetQuality fail");
	}

#ifdef GPS_ON
	if (gstGPSInfo.init == 0) {
		memset(&gstGPSInfo, 0x0, sizeof(RECORD_SERVICE_GPS_INFO_S));
		gstGPSCallback.fnGpsDataCB = MEDIA_GPSCallBack;
		gstGPSCallback.privateData = NULL;
		GPSMNG_Register(&gstGPSCallback);
		gstGPSInfo.init = 1;
	}
#endif

	return 0;
}
#endif

#ifdef SERVICES_PHOTO_ON
int32_t MEDIA_PhotoSerDeInit(void)
{
	int32_t i = 0;
	int32_t s32Ret = 0;
	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		PHOTO_SERVICE_HANDLE_T *photoSerhdl = &MEDIA_GetCtx()->SysServices.PhotoHdl[i];
		if (*photoSerhdl != NULL) {
			s32Ret = PHOTO_SERVICE_Destroy(*photoSerhdl);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "PHOTO_SERVICE_Destroy fail");
			*photoSerhdl = NULL;
		}
	}

#ifdef GPS_ON
	if (gstGPSInfo.init == 1) {
		GPSMNG_UnRegister(&gstGPSCallback);
		gstGPSInfo.init = 0;
	}
#endif

	return 0;
}
#endif

#ifdef SERVICES_ADAS_ON
int32_t MEDIA_ADASInit(void)
{
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_ADAS_ATTR_S ADASAttr = {0};
	PARAM_GetADASConfigParam(&ADASAttr);
#ifdef __CV184X__
	s32Ret = OSAL_FS_Insmod(KOMOD_PATH "/bmtpu.ko", NULL);
#else
	s32Ret = OSAL_FS_Insmod(KOMOD_PATH "/cv181x_tpu.ko", NULL);
#endif
	MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "ADAS_SERVICE_Create fail");
	for (int32_t i = 0; i < ADASAttr.adas_cnt; i++) {
		if (ADASAttr.ChnAttrs[i].Enable == false) {
			continue;
		}
		// #ifdef RESET_MODE_AHD_HOTPLUG_ON
		//     if (MEDIA_Is_CameraEnabled(i) == false) {
		//         continue;
		//     }
		// #endif
		ADAS_SERVICE_HANDLE_T *ADASSerhdl = &MediaParams->SysServices.ADASHdl[i];
		ADAS_SERVICE_PARAM_S *ADASParam = &MediaParams->SysServices.ADASParams[i];
		ADASParam->camid = i;
		ADASParam->adas_voice_event_cb = ADASMNG_VoiceCallback;
		ADASParam->adas_label_event_cb = ADASMNG_LabelCallback;
		memcpy(&ADASParam->stADASModelParam, &ADASAttr.stADASModelAttr, sizeof(ADASAttr.stADASModelAttr));

		// PARAM_MEDIA_SPEC_S params;
		// PARAM_GetMediaMode(i, &params);
		// for (int32_t j = 0; j < MAPI_VPROC_MAX_CHN_NUM; j++) {
		//     if(params.VprocAttr.VprocChnAttr[j].VprocChnid == ADASAttr.ChnAttrs[i].BindVprocChnId)
		//     {
		//         ADASParam->stVPSSParam.isExtVproc = 0;
		//         break;
		//     }else if(params.VprocAttr.ExtChnAttr[j].ChnAttr.ChnId == ADASAttr.ChnAttrs[i].BindVprocChnId)
		//     {
		//         ADASParam->stVPSSParam.isExtVproc = 1;
		//         break;
		//     }else{
		//         continue;
		//     }
		// }
		ADASParam->stVPSSParam.vprocChnId = ADASAttr.ChnAttrs[i].BindVprocChnId;
		ADASParam->stVPSSParam.vprocId = i;
		for (int32_t z = 0; z < MAX_VPROC_CNT; z++) {
			if (Syshdl->vproc[z] != NULL && ADASAttr.ChnAttrs[i].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(Syshdl->vproc[z])) {
				ADASParam->stVPSSParam.vprocHandle = Syshdl->vproc[z];
				break;
			}
		}

		s32Ret = ADAS_SERVICE_Create(ADASSerhdl, ADASParam);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "ADAS_SERVICE_Create fail");
	}

	return 0;
}

int32_t MEDIA_ADASDeInit(void)
{
	PARAM_ADAS_ATTR_S ADASAttr = {0};
	PARAM_GetADASConfigParam(&ADASAttr);
	int32_t s32Ret = 0;
	for (int32_t i = 0; i < ADASAttr.adas_cnt; i++) {
		// #ifdef RESET_MODE_AHD_HOTPLUG_ON
		//     if (MEDIA_Is_CameraEnabled(i) == false) {
		//         continue;
		//     }
		// #endif
		ADAS_SERVICE_HANDLE_T *ADASSerhdl = &MEDIA_GetCtx()->SysServices.ADASHdl[i];
		if (*ADASSerhdl != NULL) {
			s32Ret = ADAS_SERVICE_Destroy(*ADASSerhdl);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "ADAS_SERVICE_Destroy fail");
			*ADASSerhdl = NULL;
		}
	}

#ifdef __CV184X__
	OSAL_FS_Rmmod(KOMOD_PATH "/bmtpu.ko");
#else
	OSAL_FS_Rmmod(KOMOD_PATH "/cv181x_tpu.ko");
#endif
	return 0;
}
#endif

#ifdef SERVICES_QRCODE_ON

int32_t MEDIA_QRCodeInit(void)
{
	int32_t s32Ret = 0;
	MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_QRCODE_ATTR_S QRCODEAttr = {0};
	PARAM_GetQRCodeConfigParam(&QRCODEAttr);
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES && i < (uint32_t)QRCODEAttr.qrcode_cnt; i++) {
		PARAM_MEDIA_SPEC_S params;
		PARAM_GetMediaMode(i, &params);
		QRCODE_SERVICE_PARAM_S *attr = &MediaParams->SysServices.QRCodeParams[i];
		if (QRCODEAttr.ChnAttrs[i].Enable == 0) {
			continue;
		}

		for (int32_t j = 0; j < MAPI_VPROC_MAX_CHN_NUM; j++) {
			if (params.VprocAttr.VprocChnAttr[j].VprocChnEnable == true && params.VprocAttr.VprocChnAttr[j].VprocChnid == QRCODEAttr.ChnAttrs[i].BindVprocChnId) {
				attr->w = params.VprocAttr.VprocChnAttr[j].VpssChnAttr.u32Width;
				attr->h = params.VprocAttr.VprocChnAttr[j].VpssChnAttr.u32Height;
			} else if (params.VprocAttr.ExtChnAttr[j].ChnEnable == true && params.VprocAttr.ExtChnAttr[j].ChnAttr.ChnId == QRCODEAttr.ChnAttrs[i].BindVprocChnId) {
				attr->w = params.VprocAttr.ExtChnAttr[j].ChnAttr.VpssChnAttr.u32Width;
				attr->h = params.VprocAttr.ExtChnAttr[j].ChnAttr.VpssChnAttr.u32Height;
			} else {
				continue;
			}
		}
		CVI_LOGI("attr %d %d", attr->w, attr->h);
		attr->vproc_chnid = QRCODEAttr.ChnAttrs[i].BindVprocChnId;
		for (int32_t z = 0; z < MAX_VPROC_CNT; z++) {
			if (Syshdl->vproc[z] != NULL && QRCODEAttr.ChnAttrs[i].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(Syshdl->vproc[z])) {
				attr->vproc = Syshdl->vproc[z];
				break;
			}
		}
		QRCODE_SERVICE_HANDLE_T *QRCodeHdl = &MEDIA_GetCtx()->SysServices.QRCodeHdl[i];
		s32Ret = QRCode_Service_Create(QRCodeHdl, attr);
		CVI_LOGI("attr %d %d", attr->w, attr->h);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "QRCode_Service_Create fail");
	}
	return 0;
}

int32_t MEDIA_QRCodeDeInit(void)
{
	PARAM_QRCODE_ATTR_S QRCODEAttr = {0};
	PARAM_GetQRCodeConfigParam(&QRCODEAttr);
	for (uint32_t i = 0; i < MAX_CAMERA_INSTANCES && i < (uint32_t)QRCODEAttr.qrcode_cnt; i++) {
		if (QRCODEAttr.ChnAttrs[i].Enable == 0) {
			continue;
		}

		QRCODE_SERVICE_HANDLE_T QRCodeHdl = MEDIA_GetCtx()->SysServices.QRCodeHdl[i];
		QRCode_Service_Destroy(QRCodeHdl);
		QRCodeHdl = NULL;
	}
	return 0;
}

#endif

// 动态计算切分时间
static uint64_t recorder_CalculateDynamicSplitTime(RECORD_SERVICE_PARAM_S *param)
{
    uint64_t u64SplitTimeLenMSec = 0;

    // 获取SD卡信息
    STG_FS_INFO_S stFSInfo = {0};
    int32_t s32Ret = STORAGEMNG_GetFSInfo(&stFSInfo);
    if (s32Ret == 0) {
        // 获取可用空间（字节）
        uint64_t availableSpaceBytes = stFSInfo.u64AvailableSize;
        // 3GB切分
        uint64_t splitSize = 3221225472;

        if (availableSpaceBytes < splitSize) {
            splitSize = availableSpaceBytes;
        }

        // 获取视频码率
        uint32_t videoBitrate = param->bitrate_kbps * 1000;

        // 获取音频码率
        uint32_t audioBitrate = 0;
        uint32_t bitpersample = 16;
        if (param->audio_recorder_enable == true) {
            uint32_t sampleRate = param->audio_sample_rate;
            uint32_t channels = param->audio_channels;

            audioBitrate = sampleRate * bitpersample * channels;
        }

        uint32_t totalBitrate = videoBitrate + audioBitrate;

        // 计算可用录制时长（秒）
        double recordDurationSeconds = (double)splitSize / (totalBitrate / 8.0);

        // 使用可用录制时长作为切分时间（毫秒）
        uint64_t calculatedSplitTimeMs = (uint64_t)(recordDurationSeconds * 1000);

        u64SplitTimeLenMSec = calculatedSplitTimeMs;

        CVI_LOGI("Dynamic split: space=%llu bytes, bitrate=%u bps, duration=%.1f s, split=%llu ms",
            availableSpaceBytes, totalBitrate, recordDurationSeconds, calculatedSplitTimeMs);
    } else {
        CVI_LOGE("get fs info failed, use default split time 60000ms\n");
        u64SplitTimeLenMSec = 60000;
    }

    return u64SplitTimeLenMSec;
}

int32_t MEDIA_RecordSerInit(void)
{
	int32_t i = 0, j = 0, z = 0;
	int32_t s32Ret = 0;
	PARAM_MEDIA_COMM_S mediacomm;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PARAM_GetMediaComm(&mediacomm);
	PARAM_FILEMNG_S FileMng;
	PARAM_GetFileMngParam(&FileMng);
	STG_DEVINFO_S SDParam = {0};
	PARAM_GetStgInfoParam(&SDParam);
	PARAM_DEVMNG_S devmng = {0};
	PARAM_GetDevParam(&devmng);
	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		RECORD_SERVICE_HANDLE_T *recSerhdl = &MediaParams->SysServices.RecordHdl[i];
		RECORD_SERVICE_PARAM_S *param = &MediaParams->SysServices.RecordParams[i];
		PARAM_RECORD_CHN_ATTR_S *recattr = &mediacomm.Record.ChnAttrs[i];
		PARAM_THUMBNAIL_CHN_ATTR_S *thumbnail_attr = &mediacomm.Thumbnail.ChnAttrs[i];
		PARAM_PIV_CHN_ATTR_S *pivattr = &mediacomm.Piv.ChnAttrs[i];
		uint32_t prealloclen = FileMng.FileMng.dir_param[i].prealloc_sizeMB[FILEMNG_DIR_PHOTO];
		PARAM_MEDIA_SPEC_S params;

		if (recattr->Enable == true) {
			param->enable_subvideo = recattr->Subvideoen;
			param->enable_record_on_start = false;
			param->enable_perf_on_start = false;
			param->enable_debug_on_start = false;
			param->recorder_id = i;
			param->recorder_file_type = recattr->FileType;
			param->recorder_audio_codec = get_audio_codec_type(recattr->FileType);
			param->audio_recorder_enable = recattr->AudioStatus;
			param->event_recorder_pre_recording_sec = recattr->PreTime;
			param->event_recorder_post_recording_sec = recattr->PostTime;
			// param->recorder_split_interval_ms = recattr->SplitTime;
			param->timelapse_recorder_framerate = recattr->TimelapseFps;
			param->timelapse_recorder_gop_interval = recattr->TimelapseGop;
			param->normal_extend_video_buffer_sec = recattr->NormalExtendVideoBufferSec;
			param->event_extend_video_buffer_sec = recattr->EventExtendVideoBufferSec;
			param->extend_other_buffer_sec = recattr->ExtendOtherBufferSec;
			strncpy(param->devmodel, recattr->devmodel, sizeof(param->devmodel) - 1);
			param->short_file_ms = recattr->ShortFileMs;
			param->memory_buffer_sec = recattr->MemoryBufferSec;
			param->cont_recorder_event_cb = RECORDMNG_ContCallBack;
			param->event_recorder_event_cb = RECORDMNG_EventCallBack;
			param->timelapse_recorder_event_cb = RECORDMNG_ContCallBack;
			param->enable_subtitle = false;
			param->get_subtitle_cb = RECORD_GetSubtitleCallBack;
			param->generate_filename_cb = FILEMNG_GenerateFileName;
			param->enable_thumbnail = true;
			param->normal_dir_type[0] = FILEMNG_DIR_NORMAL;
			param->normal_dir_type[1] = FILEMNG_DIR_NORMAL_S;
			param->park_dir_type[0] = FILEMNG_DIR_PARK;
			param->park_dir_type[1] = FILEMNG_DIR_PARK_S;
			param->event_dir_type[0] = FILEMNG_DIR_EMR;
			param->event_dir_type[1] = FILEMNG_DIR_EMR_S;
			param->snap_dir_type = FILEMNG_DIR_PHOTO;
			strncpy(param->mntpath, SDParam.aszMntPath, strlen(SDParam.aszMntPath));
			param->flash_led_gpio = devmng.FlashLed.GpioNum;
			param->flash_led_pulse = devmng.FlashLed.Pulse;
			param->focus_pos = recattr->FocusPos;
			param->focus_pos_lock = recattr->FocusPosLock;
#ifdef GPS_ON
			param->get_gps_info_cb = (void *)MEDIA_GetGPSInfo;
			param->enable_subtitle = true;
#endif

			if (FileMng.FileMng.prealloc_param.en == true) {
				param->pre_alloc_unit = 0; // FileMng.FileMng.dir_param[i].align_size[FILEMNG_DIR_NORMAL];
			} else {
				param->pre_alloc_unit = recattr->PreallocUnit;
			}

			param->prealloclen = prealloclen;
			if (param->recorder_audio_codec == RECORD_SERVICE_AUDIO_CODEC_PCM) {
				MAPI_ACAP_ATTR_S attr = {0};
				PARAM_GetAiParam(&attr);

				param->audio_sample_rate = attr.enSampleRate;
				if (attr.bVqeOn == 1) {
					param->audio_channels = 1;
				} else {
					param->audio_channels = attr.AudioChannel;
				}
				param->audio_num_per_frame = attr.u32PtNumPerFrm;
			} else if (param->recorder_audio_codec == RECORD_SERVICE_AUDIO_CODEC_AAC) {
				MAPI_AENC_ATTR_S attr = {0};
				PARAM_GetAencParam(&attr);

				param->audio_sample_rate = attr.src_samplerate;
				param->audio_channels = attr.channels;
				param->audio_num_per_frame = attr.u32PtNumPerFrm;
			}

			for (int32_t n = 0; n < MAX_CAMERA_INSTANCES; n++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
				if (MEDIA_Is_CameraEnabled(i) == false) {
					continue;
				}
#endif
				PARAM_GetMediaMode(n, &params);

				for (j = 0; j < MAX_VENC_CNT; j++) {
					if (params.VencAttr.VencChnAttr[j].VencChnEnable == true) {
						if (recattr->Enable == true && params.VencAttr.VencChnAttr[j].VencId == recattr->BindVencId) {
							param->rec_width = params.VencAttr.VencChnAttr[j].MapiVencAttr.width;
							param->rec_height = params.VencAttr.VencChnAttr[j].MapiVencAttr.height;
							param->vproc_chn_id_venc = params.VencAttr.VencChnAttr[j].BindVprocChnId;
							param->venc_bind_mode = params.VencAttr.VencChnAttr[j].bindMode;
							param->framerate = params.VencAttr.VencChnAttr[j].framerate;
							param->gop = params.VencAttr.VencChnAttr[j].MapiVencAttr.gop;
							param->bitrate_kbps = params.VencAttr.VencChnAttr[j].MapiVencAttr.bitrate_kbps;
							param->rec_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
							param->vproc_id_rec = params.VencAttr.VencChnAttr[j].BindVprocId;
							param->recorder_video_codec = params.VencAttr.VencChnAttr[j].MapiVencAttr.codec;
							for (z = 0; z < MAX_VPROC_CNT; z++) {
								if ((MediaParams->SysHandle.vproc[z] != NULL) &&
									(params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
									param->rec_vproc = MediaParams->SysHandle.vproc[z];
									break;
								}
							}
						}
						if (recattr->Enable == true && recattr->Subvideoen == true && params.VencAttr.VencChnAttr[j].VencId == recattr->SubBindVencId){
                            param->sub_rec_venc_hdl = MediaParams->SysHandle.venchdl[n][j];/*new*/
                            param->sub_vproc_chn_id_venc = params.VencAttr.VencChnAttr[j].BindVprocChnId;
                            param->sub_bitrate_kbps = params.VencAttr.VencChnAttr[j].MapiVencAttr.bitrate_kbps;
                            param->sub_rec_width = params.VencAttr.VencChnAttr[j].MapiVencAttr.width;
                            param->sub_rec_height = params.VencAttr.VencChnAttr[j].MapiVencAttr.height;
                            param->sub_framerate = params.VencAttr.VencChnAttr[j].framerate;
                            param->sub_gop = params.VencAttr.VencChnAttr[j].MapiVencAttr.gop;
							param->sub_recorder_video_codec = params.VencAttr.VencChnAttr[j].MapiVencAttr.codec;

                            for (z = 0; z < MAX_VPROC_CNT; z++) {
                                if ((MediaParams->SysHandle.vproc[z] != NULL) &&
                                    (params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
                                    param->sub_rec_vproc = MediaParams->SysHandle.vproc[z];
                                    break;
                                }
                            }
                        }

						if (recattr->Enable == true && params.VencAttr.VencChnAttr[j].VencId == thumbnail_attr->BindVencId) {
							param->thumbnail_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
							param->vproc_chn_id_thumbnail = params.VencAttr.VencChnAttr[j].BindVprocChnId;
							param->thumbnail_bufsize = params.VencAttr.VencChnAttr[j].MapiVencAttr.bufSize;
							for (z = 0; z < MAX_VPROC_CNT; z++) {
								if ((MediaParams->SysHandle.vproc[z] != NULL) &&
									(params.VencAttr.VencChnAttr[j].BindVprocId == (uint32_t)MAPI_VPROC_GetGrp(MediaParams->SysHandle.vproc[z]))) {
									param->thumbnail_vproc = MediaParams->SysHandle.vproc[z];
									break;
								}
							}
						}

						if (recattr->Enable == true && params.VencAttr.VencChnAttr[j].VencId == pivattr->BindVencId) {
							param->piv_venc_hdl = MediaParams->SysHandle.venchdl[n][j];
							param->piv_bufsize = params.VencAttr.VencChnAttr[j].MapiVencAttr.bufSize;
						}
					}
				}
			}
			param->recorder_split_interval_ms = recorder_CalculateDynamicSplitTime(param);
			s32Ret = RECORD_SERVICE_Create(recSerhdl, param);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "RECORD_SERVICE_Create fail");

			/* lock focus with focus_pos */
			if(param->focus_pos > 0) {
				ISP_FOCUS_ATTR_S focus_attr = {0};
				s32Ret = CVI_ISP_GetAFAttr(i, &focus_attr);
				if(s32Ret != CVI_SUCCESS){
					CVI_LOGE("ISP_GetAFAttr failed with %#x", s32Ret);
				}
				CVI_LOGI("lock fouch with %d", param->focus_pos);
				focus_attr.enOpType = OP_TYPE_MANUAL;
				focus_attr.stManual.enManualOpType = OP_TYPE_FOCUS_POS;
				focus_attr.stManual.u16ManualPos = param->focus_pos;
				s32Ret = CVI_ISP_SetAFAttr(i, &focus_attr);
				if(s32Ret != CVI_SUCCESS){
					CVI_LOGE("ISP_SetAFAttr failed with %#x", s32Ret);
				}
			}
		}
	}

#ifdef GPS_ON
	if (gstGPSInfo.init == 0) {
		memset(&gstGPSInfo, 0x0, sizeof(RECORD_SERVICE_GPS_INFO_S));
		gstGPSCallback.fnGpsDataCB = MEDIA_GPSCallBack;
		gstGPSCallback.privateData = NULL;
		GPSMNG_Register(&gstGPSCallback);
		gstGPSInfo.init = 1;
	}
#endif

	return 0;
}

int32_t MEDIA_RecordSerDeInit(void)
{
	int32_t i = 0;
	int32_t s32Ret = 0;
	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		RECORD_SERVICE_HANDLE_T *recSerhdl = &MEDIA_GetCtx()->SysServices.RecordHdl[i];
		if (*recSerhdl != NULL) {
			/* clsoe flash led */
			RECORD_SERVICE_SetFlashLed(*recSerhdl, RECORD_SERVICE_FLASH_LED_MODE_NC);
			s32Ret = RECORD_SERVICE_Destroy(*recSerhdl);
			MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "RECORD_SERVICE_Destroy fail");
			*recSerhdl = NULL;
		}

		/* unlock focus */
		ISP_FOCUS_ATTR_S focus_attr = {0};
		s32Ret = CVI_ISP_GetAFAttr(i, &focus_attr);
		if(s32Ret != CVI_SUCCESS){
			CVI_LOGE("ISP_GetAFAttr failed with %#x", s32Ret);
		}
		CVI_LOGI("unlock focus");
		focus_attr.enOpType = OP_TYPE_AUTO;
		focus_attr.stManual.enManualOpType = OP_TYPE_AUTO_FOCUS;
		s32Ret = CVI_ISP_SetAFAttr(i, &focus_attr);
		if(s32Ret != CVI_SUCCESS){
			CVI_LOGE("ISP_SetAFAttr failed with %#x", s32Ret);
		}
	}

#ifdef GPS_ON
	if (gstGPSInfo.init == 1) {
		GPSMNG_UnRegister(&gstGPSCallback);
		gstGPSInfo.init = 0;
	}
#endif

	return 0;
}

int32_t MEDIA_PlayBackSerInit(void)
{
#ifdef SERVICES_PLAYER_ON
	PARAM_MEDIA_COMM_S mediacomm;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PLAYER_SERVICE_HANDLE_T *PlaySerhdl = &MediaParams->SysServices.PsHdl;
	PLAYER_SERVICE_PARAM_S *param = &MediaParams->SysServices.PsParam;

	PARAM_GetMediaComm(&mediacomm);
	param->chn_id = 0;
	param->repeat = false;
	param->x = 0;
	param->y = 0;
	param->width = mediacomm.Vo.Width;
	param->height = mediacomm.Vo.Height;
	param->disp_rotate = mediacomm.Vo.Rotate;
	param->disp_fmt = mediacomm.Vo.frame_fmt; // 12 yuv422 //13 yuv420;
	param->disp = MediaParams->SysHandle.dispHdl;
	param->ao = MediaParams->SysHandle.aohdl;
	param->SampleRate = mediacomm.Ao.enSampleRate;
	param->AudioChannel = mediacomm.Ao.AudioChannel;
	param->disp_aspect_ratio = ASPECT_RATIO_AUTO;

	PLAYER_SERVICE_Create(PlaySerhdl, param);
#endif

	return 0;
}

int32_t MEDIA_PlayBackSerDeInit(void)
{
#ifdef SERVICES_PLAYER_ON
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();
	PLAYER_SERVICE_HANDLE_T PlaySerhdl = MediaParams->SysServices.PsHdl;

	PLAYER_SERVICE_Destroy(&PlaySerhdl);
#endif
	return 0;
}

int32_t MEDIA_SetAntiFlicker(void)
{
	PARAM_MENU_S *Param = (PARAM_MENU_S *)malloc(sizeof(PARAM_MENU_S));
	if (Param == NULL) {
		printf("%s, %d malloc fail\n", __func__, __LINE__);
		return -1;
	}
	memset(Param, 0, sizeof(PARAM_MENU_S));

	PARAM_GetMenuParam(Param);
	for (int32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		uint8_t enable, frequency;
		switch (Param->Frequence.Current) {
		case MENU_FREQUENCY_OFF:
			enable = 0;
			frequency = 0;
			break;
		case MENU_FREQUENCY_50:
			enable = 1;
			frequency = 50;
			break;
		case MENU_FREQUENCY_60:
			enable = 1;
			frequency = 60;
			break;
		default:
			enable = 0;
			frequency = 0;
			break;
		}
		CVI_ISP_SetAntiFlicker(i, enable, frequency);
	}
	free(Param);

	return 0;
}

int32_t MEDIA_SetLightFrequence(void)
{
	int32_t s32Ret = -1;
	PARAM_CONTEXT_S *pstParamCtx = PARAM_GetCtx();
	int32_t light_frequence = pstParamCtx->pstCfg->Menu.LightFrequence.Current;

	for (int32_t i = 0; i < MAX_DEV_INSTANCES; i++) {
#ifdef RESET_MODE_AHD_HOTPLUG_ON
		if (MEDIA_Is_CameraEnabled(i) == false) {
			continue;
		}
#endif
		ISP_EXPOSURE_ATTR_S stExpAttr = {0};
		uint8_t enable = 0, freq = 0;

		switch (light_frequence) {
			case 0:
				enable = 1;
				freq = 50; // 50Hz
				break;
			case 1:
				enable = 1;
				freq = 60; // 60Hz
				break;
			default:
				enable = 0;
				freq = 50;
				break;
		}

		s32Ret = CVI_ISP_GetExposureAttr(i, &stExpAttr);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "GetExposureAttr fail");
		/* manual iso doesn't support anti flicker */
		if(stExpAttr.stAuto.stISONumRange.u32Max == stExpAttr.stAuto.stISONumRange.u32Min){
			enable = 0;
		}

		s32Ret = CVI_ISP_SetAntiFlicker(i, enable, freq);
		MEDIA_CHECK_RET(s32Ret, APP_MEDIA_EINVAL, "SetAntiFlicker fail");

		CVI_LOGI("antiFlocker enable:%d freq:%d", enable, freq);
	}
	return 0;
}

int32_t MEDIA_SensorSwitchInit(PARAM_MEDIA_SNS_ATTR_S *psns_attr,
							PARAM_MEDIA_VACP_ATTR_S *pvcap_attr){
	static PARAM_MEDIA_SNS_ATTR_S sns_attr = {0};
	static PARAM_MEDIA_VACP_ATTR_S vcap_attr = {0};
	static uint32_t has_init_param = 0;
	uint32_t cam_id = 0;

	if(psns_attr != NULL && pvcap_attr != NULL){
		CVI_LOGI("save switch param");
		memcpy(&sns_attr, psns_attr, sizeof(PARAM_MEDIA_SNS_ATTR_S));
		memcpy(&vcap_attr, pvcap_attr, sizeof(PARAM_MEDIA_VACP_ATTR_S));
		has_init_param = 1;
	}else{
		for (cam_id = 0; cam_id < MAX_CAMERA_INSTANCES; cam_id++) {
			if (!MEDIA_Is_CameraEnabled(cam_id)) {
				continue;
			}
			if(has_init_param){
				CVI_LOGI("set switch param");
				PARAM_SetSensorParam(cam_id, &sns_attr);
				PARAM_SetVcapParam(cam_id, &vcap_attr);
			} else {
				CVI_LOGI("get and save switch param");
				PARAM_GetSensorParam(cam_id, &sns_attr);
				PARAM_GetVcapParam(cam_id, &vcap_attr);
				has_init_param = 1;
			}
		}
	}

	/* configure mipi switch */
	HAL_GPIO_Set_Value(sns_attr.SnsChnAttr.u32LaneSwitchPin, sns_attr.SnsChnAttr.u32LaneSwitchPinPol);
	CVI_LOGI("cfg mipi switch pin: %d, val: %d",
			sns_attr.SnsChnAttr.u32LaneSwitchPin, sns_attr.SnsChnAttr.u32LaneSwitchPinPol);

	return 0;
}

#ifdef SERVICES_FACEP_ON
int32_t MEDIA_FacepInit(SmileFun smile_callback_fun){
	int32_t i = 0;
	PARAM_FACEP_ATTR_S *FacepAttr = &PARAM_GetCtx()->pstCfg->MediaComm.Facep;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();

	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
			if (MEDIA_Is_CameraEnabled(i) == false) {
				continue;
			}
		#endif
		FACEP_SERVICE_HANDLE_T *hdl = &MediaParams->SysServices.FacepHdl[i];
		FACEP_SERVICE_PARAM_S *param = &MediaParams->SysServices.FacepParams[i];

		memcpy(param, &FacepAttr->sv_param[i], sizeof(FACEP_SERVICE_PARAM_S));
		FACEP_SERVICE_Create(hdl, param);
		FACEP_SERVICE_Register_Smile_Callback(smile_callback_fun);
	}
	return 0;
}

int32_t MEDIA_FacepDeInit(void){
// #ifdef SERVICES_FACEP_ON
	int32_t i = 0;
	MEDIA_PARAM_INIT_S *MediaParams = MEDIA_GetCtx();

	for (i = 0; i < MAX_CAMERA_INSTANCES; i++) {
		#ifdef RESET_MODE_AHD_HOTPLUG_ON
			if (MEDIA_Is_CameraEnabled(i) == false) {
				continue;
			}
		#endif
		FACEP_SERVICE_HANDLE_T hdl = MediaParams->SysServices.FacepHdl[i];
		FACEP_SERVICE_Destory(hdl);
	}
// #endif
	return 0;
}
#endif
