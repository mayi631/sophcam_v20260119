#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <unistd.h>

#include "record_service.h"
#include "rs_master.h"
#include "cvi_log.h"
#include "zoomp.h"

static REC_ATTR_T gst_rec_attr[MAX_CONTEXT_CNT];


static int32_t rs_get_rec_id(RECORD_SERVICE_HANDLE_T rs)
{
    for (int32_t i = 0; i < MAX_CONTEXT_CNT; i++) {
        if (gst_rec_attr[i].rs == rs) {
            return i;
        }
    }
    return MAX_CONTEXT_CNT;
}

/* MAPI framerate: integer fps, or (denom<<16)|num for fractional. */
static float rs_framerate_to_fps(uint32_t fr)
{
    if (fr == 0)
        return 0.f;
    if ((fr & 0xffff0000u) == 0)
        return (float)fr;
    uint32_t num = fr & 0xffffu;
    uint32_t denom = fr >> 16;
    if (denom == 0)
        return (float)fr;
    return (float)num / (float)denom;
}

/*
 * MOV/MP4 timeline is sensitive to track fps metadata.
 * If configured mode fps (e.g. 60) drifts from actual encoder dst fps under load/drop,
 * file duration can become shorter than wall-clock OSD. Override with live VENC attrs.
 */
static void rs_overlay_video_track_fps_from_venc(RECORD_SERVICE_PARAM_S *aattr, REC_ATTR_T *rattr)
{
    if (rattr->enRecType != RECORDER_TYPE_NORMAL)
        return;

    if (aattr->rec_venc_hdl != NULL) {
        MAPI_VENC_CHN_ATTR_T va = {0};
        if (MAPI_VENC_GetAttr(aattr->rec_venc_hdl, &va) == 0) {
            float fps = rs_framerate_to_fps(va.venc_param.dst_framerate);
            if (fps >= 1.f) {
                RECORDER_TRACK_SOURCE_S *vh =
                    &rattr->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO];
                CVI_LOGI("mux main video: VENC dst fps=%.3f (param %.3f)", fps, aattr->framerate);
                vh->unTrackSourceAttr.stVideoInfo.fFrameRate = fps;
                vh->unTrackSourceAttr.stVideoInfo.fSpeed = fps;
                if (va.venc_param.gop > 0)
                    vh->unTrackSourceAttr.stVideoInfo.u32Gop = (uint32_t)va.venc_param.gop;
            }
        }
    }

    if (aattr->enable_subvideo && aattr->sub_rec_venc_hdl != NULL) {
        MAPI_VENC_CHN_ATTR_T sva = {0};
        if (MAPI_VENC_GetAttr(aattr->sub_rec_venc_hdl, &sva) == 0) {
            float sfps = rs_framerate_to_fps(sva.venc_param.dst_framerate);
            if (sfps >= 1.f) {
                RECORDER_TRACK_SOURCE_S *sh =
                    &rattr->astStreamAttr[0].aHTrackSrcHandle[CVI_RECORDER_TRACK_SOURCE_TYPE_SUB_VIDEO];
                CVI_LOGI("mux sub video: VENC dst fps=%.3f (param %.3f)", sfps, aattr->sub_framerate);
                sh->unTrackSourceAttr.stVideoInfo.fFrameRate = sfps;
                sh->unTrackSourceAttr.stVideoInfo.fSpeed = sfps;
                if (sva.venc_param.gop > 0)
                    sh->unTrackSourceAttr.stVideoInfo.u32Gop = (uint32_t)sva.venc_param.gop;
            }
        }
    }
}

static void APPATTR_2_RECORDATTR(RECORD_SERVICE_PARAM_S *aattr, REC_ATTR_T *rattr)
{
    CVI_LOGD("rec_mode = %d", aattr->rec_mode);

    if (aattr->timelapse_recorder_gop_interval == 0) {
        rattr->enRecType = RECORDER_TYPE_NORMAL;
    } else {
        rattr->enRecType = RECORDER_TYPE_LAPSE;
        rattr->unRecAttr.stLapseRecAttr.u32IntervalMs = aattr->timelapse_recorder_gop_interval * 1000;
        rattr->unRecAttr.stLapseRecAttr.fFramerate = aattr->timelapse_recorder_framerate;
    }

    if (aattr->recorder_split_interval_ms == 0) {
        rattr->stSplitAttr.enSplitType = RECORDER_SPLIT_TYPE_NONE;
    } else {
        rattr->stSplitAttr.enSplitType = RECORDER_SPLIT_TYPE_TIME;
        rattr->stSplitAttr.u64SplitTimeLenMSec = aattr->recorder_split_interval_ms;
    }
    CVI_LOGD("enRecType = %d, enSplitType %d, u64SplitTimeLenMSec = %"PRIu64"", rattr->enRecType, rattr->stSplitAttr.enSplitType, rattr->stSplitAttr.u64SplitTimeLenMSec);

    rattr->u32StreamCnt = 1;
    uint32_t u32StreamIdx = 0;
    uint32_t u32TrackCnt = 0;

    for (u32StreamIdx = 0; u32StreamIdx < rattr->u32StreamCnt; u32StreamIdx++) {
        RECORDER_TRACK_SOURCE_S *handle = &rattr->astStreamAttr[u32StreamIdx].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO];
        handle->enTrackType = RECORDER_TRACK_SOURCE_TYPE_VIDEO;
        handle->enable = 1;
        if (aattr->recorder_video_codec == RECORD_SERVICE_VIDEO_CODEC_H264) {
            handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_H264;
        } else if (aattr->recorder_video_codec == RECORD_SERVICE_VIDEO_CODEC_H265) {
            handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_H265;
        } else {
            handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_MJPEG;
        }
        handle->unTrackSourceAttr.stVideoInfo.u32Height = aattr->rec_height;
        handle->unTrackSourceAttr.stVideoInfo.u32Width = aattr->rec_width;
        handle->unTrackSourceAttr.stVideoInfo.u32BitRate = (uint32_t)aattr->bitrate_kbps;
        handle->unTrackSourceAttr.stVideoInfo.fFrameRate = aattr->framerate;
        handle->unTrackSourceAttr.stVideoInfo.u32Gop = aattr->gop;
        handle->unTrackSourceAttr.stVideoInfo.fSpeed = aattr->framerate;
        u32TrackCnt++;

        RECORDER_TRACK_SOURCE_S *sub_handle = &rattr->astStreamAttr[u32StreamIdx].aHTrackSrcHandle[CVI_RECORDER_TRACK_SOURCE_TYPE_SUB_VIDEO];
        sub_handle->enTrackType = CVI_RECORDER_TRACK_SOURCE_TYPE_SUB_VIDEO;
        sub_handle->enable = aattr->enable_subvideo;
        if(sub_handle->enable == 1){
            if (aattr->sub_recorder_video_codec == RECORD_SERVICE_VIDEO_CODEC_H264) {
                sub_handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_H264;
            } else if (aattr->sub_recorder_video_codec == RECORD_SERVICE_VIDEO_CODEC_H265) {
                sub_handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_H265;
            } else {
                sub_handle->unTrackSourceAttr.stVideoInfo.enCodecType = MUXER_TRACK_VIDEO_CODEC_MJPEG;
            }
            sub_handle->unTrackSourceAttr.stVideoInfo.u32Height = aattr->sub_rec_height;
            sub_handle->unTrackSourceAttr.stVideoInfo.u32Width = aattr->sub_rec_width;
            sub_handle->unTrackSourceAttr.stVideoInfo.u32BitRate = aattr->sub_bitrate_kbps;
            sub_handle->unTrackSourceAttr.stVideoInfo.fFrameRate = aattr->sub_framerate;
            sub_handle->unTrackSourceAttr.stVideoInfo.u32Gop = aattr->sub_gop;
            sub_handle->unTrackSourceAttr.stVideoInfo.fSpeed = aattr->sub_framerate;
            u32TrackCnt++;
        }

        handle = &rattr->astStreamAttr[u32StreamIdx].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO];
        handle->enTrackType = RECORDER_TRACK_SOURCE_TYPE_AUDIO;
        handle->enable = aattr->audio_recorder_enable;
        if (rattr->enRecType == RECORDER_TYPE_LAPSE) {
            handle->enable = 0;
        }

        CVI_LOGD("enable %d, enRecType %d", handle->enable, rattr->enRecType);
        if (aattr->recorder_audio_codec == RECORD_SERVICE_AUDIO_CODEC_PCM) {
            handle->unTrackSourceAttr.stAudioInfo.enCodecType = MUXER_TRACK_AUDIO_CODEC_ADPCM;
        } else if (aattr->recorder_audio_codec == RECORD_SERVICE_AUDIO_CODEC_AAC) {
            handle->unTrackSourceAttr.stAudioInfo.enCodecType = MUXER_TRACK_AUDIO_CODEC_AAC;
        } else {
            /*do*/
        }
        handle->unTrackSourceAttr.stAudioInfo.u32SampleRate = aattr->audio_sample_rate;
        handle->unTrackSourceAttr.stAudioInfo.u32ChnCnt = aattr->audio_channels;
        handle->unTrackSourceAttr.stAudioInfo.u32SamplesPerFrame = aattr->audio_num_per_frame;
        u32TrackCnt++;

        handle = &rattr->astStreamAttr[u32StreamIdx].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_PRIV];
        handle->enTrackType = RECORDER_TRACK_SOURCE_TYPE_PRIV;
        handle->enable = 1;
        u32TrackCnt++;
        rattr->astStreamAttr[u32StreamIdx].u32TrackCnt = u32TrackCnt;
    }

    rattr->handles.piv_venc_hdl = aattr->piv_venc_hdl;
    rattr->handles.piv_bufsize = aattr->piv_bufsize;

    rattr->handles.rec_vproc = aattr->rec_vproc;
    rattr->handles.thumbnail_vproc = aattr->thumbnail_vproc;
    rattr->handles.thumbnail_venc_hdl = aattr->thumbnail_venc_hdl;
    rattr->handles.thumbnail_bufsize = aattr->thumbnail_bufsize;

    rattr->handles.rec_venc_hdl = aattr->rec_venc_hdl;
    rattr->enable_subvideo = aattr->enable_subvideo;
    rattr->handles.sub_rec_venc_hdl = aattr->sub_rec_venc_hdl;
    rattr->handles.sub_rec_vproc = aattr->sub_rec_vproc;
    rattr->handles.sub_vproc_chn_id_venc = aattr->sub_vproc_chn_id_venc;
    rattr->handles.vproc_chn_id_thumbnail = aattr->vproc_chn_id_thumbnail;
    rattr->handles.vproc_chn_id_venc = aattr->vproc_chn_id_venc;
    rattr->handles.venc_bind_mode = aattr->venc_bind_mode;
    rattr->handles.venc_rec_start = 0;
    rs_overlay_video_track_fps_from_venc(aattr, rattr);

    rattr->stCallback.pfnRequestFileNames = aattr->generate_filename_cb;
    rattr->stCallback.pfnNormalRecCb = aattr->cont_recorder_event_cb;
    rattr->stCallback.pfnEventRecCb = aattr->event_recorder_event_cb;
    rattr->stCallback.pfnLapseRecCb = aattr->timelapse_recorder_event_cb;
    rattr->stCallback.pfnGetSubtitleCb = aattr->get_subtitle_cb;
    rattr->stCallback.pfnGetGPSInfoCb = aattr->get_gps_info_cb;
    rattr->s32RecPresize = aattr->pre_alloc_unit;
    rattr->s32SnapPresize = aattr->prealloclen;
    rattr->s32MemRecPreSec = aattr->memory_buffer_sec;
    rattr->enable_debug_on_start = aattr->enable_debug_on_start;
    rattr->enable_record_on_start = aattr->enable_record_on_start;
    rattr->enable_perf_on_start = aattr->enable_perf_on_start;
    rattr->enable_subtitle = aattr->enable_subtitle;
    if (rattr->enRecType == RECORDER_TYPE_LAPSE) {
        rattr->enable_subtitle = 0;
    }
    rattr->enable_thumbnail = aattr->enable_thumbnail;
    rattr->recorder_file_type = aattr->recorder_file_type;
    strncpy(rattr->recorder_save_dir_base, aattr->recorder_save_dir_base, CS_PARAM_MAX_FILENAME_LEN - 1);
    rattr->u32PreRecTimeSec = aattr->event_recorder_pre_recording_sec;
    rattr->u32PostRecTimeSec = aattr->event_recorder_post_recording_sec;
    rattr->short_file_ms = aattr->short_file_ms;
    strncpy(rattr->devmodel, aattr->devmodel, sizeof(rattr->devmodel) - 1);
    strncpy(rattr->mntpath, aattr->mntpath, strlen(aattr->mntpath));

    rattr->normal_dir_type[0] = aattr->normal_dir_type[0];
    rattr->normal_dir_type[1] = aattr->normal_dir_type[1];
    rattr->park_dir_type[0] = aattr->park_dir_type[0];
    rattr->park_dir_type[1] = aattr->park_dir_type[1];
    rattr->event_dir_type[0] = aattr->event_dir_type[0];
    rattr->event_dir_type[1] = aattr->event_dir_type[1];
    rattr->snap_dir_type = aattr->snap_dir_type;
    rattr->flash_led_gpio = aattr->flash_led_gpio;
    rattr->flash_led_pulse = aattr->flash_led_pulse;
}

/**
    pulse: 1~16, see AW3641E spec(1-wire interface)
    time_ms: the time of open
*/
static int32_t record_flash_led_by_gpio(uint32_t gpio_num, uint32_t pulse, uint32_t is_open){
	uint32_t i = 0;
	uint32_t j = -1;
	int32_t fd = -1;
	char path[64] = {0};

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if(fd < 0){
		CVI_LOGE("open failed");
		return -1;
	}
	snprintf(path, sizeof(path), "%d", gpio_num);
	write(fd, path, strlen(path));
	close(fd);

	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/direction", gpio_num);
	fd = open(path, O_WRONLY);
	if(fd < 0){
		CVI_LOGE("open failed");
		return -1;
	}
	write(fd, "out", 3);
	close(fd);

	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", gpio_num);
	fd = open(path, O_RDWR);
	if(fd < 0){
		CVI_LOGE("open failed");
		return -1;
	}

	if(is_open){
		for(i = 0; i < pulse; i++){
			lseek(fd, 0, SEEK_SET);
			write(fd, "0", 1);//2.3us
			for(j = 0; j < 500; j++);// 0.6us per 100
			lseek(fd, 0, SEEK_SET);
			write(fd, "1", 1);//2.3us
			for(j = 0; j < 500; j++);// 0.6us per 100
		}
	} else {
		write(fd, "0", 1);//2.3us
	}

	close(fd);
	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if(fd < 0){
		CVI_LOGE("open failed");
		return -1;
	}
	memset(path, 0, sizeof(path));
	snprintf(path, sizeof(path), "%d", gpio_num);
	write(fd, path, strlen(path));
	close(fd);

	return 0;
}

int32_t RECORD_SERVICE_Create(RECORD_SERVICE_HANDLE_T *hdl, RECORD_SERVICE_PARAM_S *param) {
    REC_ATTR_T *attr = &gst_rec_attr[param->recorder_id];
    APPATTR_2_RECORDATTR(param, attr);
    attr->rs = master_create(param->recorder_id, attr);
    *hdl = attr->rs;
    return (*hdl != NULL) ? 0 : -1;
}

int32_t RECORD_SERVICE_Destroy(RECORD_SERVICE_HANDLE_T hdl) {
    return master_destroy(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_AdjustFocus(RECORD_SERVICE_HANDLE_T hdl , char* ratio)
{
    int32_t ret = 0;
    VPSS_CROP_INFO_S st_crop_info = {0};
    VPSS_GRP vpss_grp = 0;
    VPSS_GRP_ATTR_S st_grp_attr = {0};
    REC_ATTR_T rsattr = gst_rec_attr[rs_get_rec_id(hdl)];
    float focus_ratio = 0;
    uint32_t chn_cnt = 0, chn_num = 0;
    RECT_S crop_in = {0}, crop_out = {0};

    focus_ratio = atof(ratio);
    vpss_grp = MAPI_VPROC_GetGrp(rsattr.handles.rec_vproc);
    CVI_LOGI("vpss_grp %d focus_ratio %f", vpss_grp, focus_ratio);

    ret = MAPI_VPROC_GetGrpAttr(rsattr.handles.rec_vproc, &st_grp_attr);
    if(ret != MAPI_SUCCESS){
        CVI_LOGE("MAPI_VPROC_GetGrpAttr failed");
        return RS_ERR_FAILURE;
    }

    ret = MAPI_VPROC_GetChnCrop(rsattr.handles.rec_vproc, 0, &st_crop_info);
    if(ret != MAPI_SUCCESS){
        CVI_LOGE("MAPI_VPROC_GetChnCrop failed");
        return RS_ERR_FAILURE;
    }

    ret = MAPI_VPROC_GetGrpChnNum(rsattr.handles.rec_vproc, &chn_num);
    if(ret != MAPI_SUCCESS){
        CVI_LOGE("MAPI_VPROC_GetGrpChnNum failed");
        return RS_ERR_FAILURE;
    }

    if(((!st_crop_info.bEnable) && focus_ratio < 2)
        || focus_ratio > ZOOM_MAX_RADIO){
        CVI_LOGI("don't need to crop: %d", st_crop_info.bEnable);
        return RS_ERR_FAILURE;
    }


    if(st_crop_info.bEnable){
		crop_in.s32X = st_crop_info.stCropRect.s32X;
		crop_in.s32Y = st_crop_info.stCropRect.s32Y;
		crop_in.u32Width = st_crop_info.stCropRect.u32Width;
		crop_in.u32Height = st_crop_info.stCropRect.u32Height;
	}else{
		crop_in.s32X = 0;
		crop_in.s32Y = 0;
		crop_in.u32Width = st_grp_attr.u32MaxW;
		crop_in.u32Height = st_grp_attr.u32MaxH;
	}

	if(!ZOOMP_Is_Init()){
		ZOOMP_Init(crop_in);
	}

	if(ZOOMP_GetCropInfo(crop_in, &crop_out, focus_ratio)){
		return RS_ERR_FAILURE;
	}

	if(crop_out.u32Width < 64 || crop_out.u32Height < 64){
		CVI_LOGE("crop is too small");
		return RS_ERR_FAILURE;
	}

    st_crop_info.bEnable = CVI_TRUE;
    st_crop_info.enCropCoordinate = VPSS_CROP_ABS_COOR;
    st_crop_info.stCropRect = crop_out;

    /* vpss online不能使用组裁剪, 使用通道裁剪模拟组裁剪 */
    for(chn_cnt = 0; chn_cnt < chn_num; chn_cnt++){
        ret = MAPI_VPROC_SetChnCrop(rsattr.handles.rec_vproc, chn_cnt, &st_crop_info);
        if(ret != MAPI_SUCCESS){
            CVI_LOGE("CVI_VPSS_SetGrpCrop failed");
            return RS_ERR_FAILURE;
        }
    }
    return 0;
}

int32_t RECORD_SERVICE_UpdateParam(RECORD_SERVICE_HANDLE_T hdl, RECORD_SERVICE_PARAM_S *param) {
    if (!param) {
        CVI_LOGE("Input recorder parameter is NULL");
        return -1;
    }

    REC_ATTR_T *attr = &gst_rec_attr[param->recorder_id];
    APPATTR_2_RECORDATTR(param, attr);
    return master_update_attr(rs_get_rec_id(hdl), attr);
}

int32_t RECORD_SERVICE_StartRecord(RECORD_SERVICE_HANDLE_T hdl) {
    if (!hdl) {
        return -1;
    }
    return master_start_normal_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StopRecord(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_stop_normal_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StartTimelapseRecord(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_start_lapse_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StopTimelapseRecord(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_stop_lapse_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_EventRecord(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_start_event_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StopEventRecord(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_stop_event_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StartMute(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_set_mute(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StopMute(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        return -1;
    }
    return master_cancle_mute(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_PivCapture(RECORD_SERVICE_HANDLE_T hdl, char *file_name)
{
    if (!hdl) {
        return -1;
    }
    return master_snap(rs_get_rec_id(hdl), file_name);
}

void RECORD_SERVICE_WaitPivFinish(RECORD_SERVICE_HANDLE_T hdl)
{
    if (!hdl) {
        CVI_LOGE("record service handle is null !\n");
        return;
    }
    master_waitsnap_finish(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StartMemoryBuffer(RECORD_SERVICE_HANDLE_T hdl) {
    if (!hdl) {
        return -1;
    }
    return master_start_mem_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_StopMemoryBuffer(RECORD_SERVICE_HANDLE_T hdl) {
    if (!hdl) {
        return -1;
    }
    return master_stop_mem_rec(rs_get_rec_id(hdl));
}

int32_t RECORD_SERVICE_SetFlashLed(RECORD_SERVICE_HANDLE_T hdl, RECORD_SERVICE_FLASH_LED_MODE_E mode)
{
    int32_t is_open = 0;
    REC_ATTR_T rsattr = gst_rec_attr[rs_get_rec_id(hdl)];

    if(mode == RECORD_SERVICE_FLASH_LED_MODE_NP){
        is_open = 1;
    }else {
        is_open = 0;
    }
    CVI_LOGI("flash(%d %d %d)", rsattr.flash_led_gpio, rsattr.flash_led_pulse, is_open);
    record_flash_led_by_gpio(rsattr.flash_led_gpio, rsattr.flash_led_pulse, is_open);
    return 0;
}
