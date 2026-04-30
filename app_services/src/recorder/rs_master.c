
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <fcntl.h>
#include <sys/select.h>
#include <inttypes.h>

/* According to earlier standards */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "dtcf.h"
#include "audio_service.h"
#include "rs_master.h"
#include "rs_context.h"
#include "mapi_sys.h"
#include "mapi_vproc.h"

#ifdef SERVICES_SUBVIDEO_ON
#include "subvideo_service.h"
#endif

#define DEUBG_VENC_STREAM_STATS
#define DEBUG_VENC_STREAM_STATS_COUNT (100)
#define SINGLE_RECORDER
// #define STORE_STREAM_COPY // resolve the piv issue for temparally
#ifdef RS_FPS_LOG
static unsigned long get_cost_ms(struct timeval t2, struct timeval t1)
{
	unsigned long cost_ms = 1000 * (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1000;
	return cost_ms;
}
#endif

#define RS_PERF_STAT_INIT()               \
	uint64_t t1, t2;                      \
	do {                                  \
		if (RS_STATE_ENABLED(rs, PERF)) { \
			OSAL_TIME_GetBootTimeUs(&t1); \
		}                                 \
	} while (0)

#define RS_PERF_STAT_ADD(stat)                 \
	do {                                       \
		if (RS_STATE_ENABLED(rs, PERF)) {      \
			OSAL_TIME_GetBootTimeUs(&t2);      \
			PERF_StatAdd((stat), t2 - t1); \
			OSAL_TIME_GetBootTimeUs(&t1);      \
		}                                      \
	} while (0)

#define RS_PERF_MARK_ADD(mark)            \
	do {                                  \
		if (RS_STATE_ENABLED(rs, PERF)) { \
			uint64_t tt;                  \
			OSAL_TIME_GetBootTimeUs(&tt); \
			PERF_MarkAdd((mark), tt); \
		}                                 \
	} while (0)

const char *FILE_TYPE_SUFFIX[RECORD_SERVICE_FILE_TYPE_MAX] = {
	".mp4",
	".mov",
	".ts",
	".es",
	".jpg",
	""};

typedef int32_t (*GENERATE_FILENAME_CB_FN_PTR)(int32_t inx, int32_t dir, const char *format, char *filename, int32_t len);

typedef struct recorder_get_filename_param_t {
	rs_context_handle_t rs;
	RECORD_SERVICE_FILE_TYPE_E file_type;
	int32_t dir_type;
} recorder_get_filename_param_t;

typedef struct thumbnail_buf_s {
	void *buf;
	uint32_t size;
	uint32_t actsize;
} thumbnail_buf_t;
static thumbnail_buf_t g_rec_thumbnail_buf[MAX_CONTEXT_CNT];
#ifdef ENABLE_SNAP_ON
static thumbnail_buf_t g_snap_thumbnail_buf[MAX_CONTEXT_CNT];
static thumbnail_buf_t g_snap_buf[MAX_CONTEXT_CNT];
#endif
static rs_context_t gstRecMasterCtx[MAX_CONTEXT_CNT];

static bool rs_thumbnail_chn_can_be_toggled(REC_ATTR_T *p)
{
	if (p == NULL || p->handles.thumbnail_vproc == NULL) {
		return false;
	}
	/* Safety: never toggle the active recording channel by mistake. */
	return p->handles.vproc_chn_id_thumbnail != p->handles.vproc_chn_id_venc;
}

static uint32_t rs_rec_main_video_width(const REC_ATTR_T *p)
{
	const RECORDER_TRACK_SOURCE_S *vid;

	if (p == NULL) {
		return 0;
	}
	vid = &p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO];
	if (!vid->enable) {
		return 0;
	}
	return vid->unTrackSourceAttr.stVideoInfo.u32Width;
}

/* VPSS needs several frames after channel enable before output is stable (especially 4K). */
static uint32_t rs_thumb_settle_delay_us(const REC_ATTR_T *p)
{
	uint32_t w = rs_rec_main_video_width(p);

	if (w >= 3840) {
		return 200 * 1000;
	}
	if (w >= 1920) {
		return 100 * 1000;
	}
	return 50 * 1000;
}

static int32_t rs_get_venc_stream(MAPI_VENC_HANDLE_T vhdl, VENC_STREAM_S *stream)
{
	int32_t ret = MAPI_VENC_GetStreamTimeWait(vhdl, stream, 1000);
	if (ret != 0) {
		CVI_LOGE("[%p]: MAPI_VENC_GetStreamTimeWait failed", vhdl);
		return -1;
	}

	if (stream->u32PackCount <= 0 || stream->u32PackCount > FRAME_STREAM_SEGMENT_MAX_NUM) {
		MAPI_VENC_ReleaseStream(vhdl, stream);
		return -1;
	}

	return 0;
}

static int32_t process_venc_rec_stream(rs_context_handle_t rs, MAPI_VENC_HANDLE_T rec_venc_hdl, MUXER_FRAME_TYPE_E type, VENC_STREAM_S *stream)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	uint8_t *thumbnail_data = (uint8_t *)g_rec_thumbnail_buf[rs->id].buf;
	uint32_t *thumbnail_len = &g_rec_thumbnail_buf[rs->id].actsize;

	RS_PERF_STAT_INIT();
	switch (p->recorder_file_type) {
	case RECORD_SERVICE_FILE_TYPE_ES:
		if (RS_STATE_ENABLED(rs, RECORD) && (rs->outfp != NULL)) {
			for (uint32_t i = 0; i < stream->u32PackCount; i++) {
				VENC_PACK_S *ppack;
				ppack = &stream->pstPack[i];
				if (ppack->u32Len - ppack->u32Offset > 0) {
					fwrite(ppack->pu8Addr + ppack->u32Offset, ppack->u32Len - ppack->u32Offset, 1, rs->outfp);
				}
			}
		}
		break;

	case RECORD_SERVICE_FILE_TYPE_MP4:
	case RECORD_SERVICE_FILE_TYPE_MOV:
	case RECORD_SERVICE_FILE_TYPE_TS: {
		RECORDER_FRAME_STREAM_S frame_stream;
		memset(&frame_stream, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
		bool iskey = 0;
		for (unsigned i = 0; i < stream->u32PackCount; i++) {
			VENC_PACK_S *ppack;
			ppack = &stream->pstPack[i];
			frame_stream.data[i] = ppack->pu8Addr + ppack->u32Offset;
			frame_stream.len[i] = ppack->u32Len - ppack->u32Offset;
			frame_stream.vi_pts[i] = stream->pstPack[i].u64PTS;
			MAPI_VENC_GetStreamStatus(rec_venc_hdl, ppack, &iskey);
			frame_stream.vftype[i] = iskey;
			if (p->enable_thumbnail && (rs->need_thumbnail & 0x2) != 0x2 && *thumbnail_len > 0) {
				frame_stream.thumbnail_len = *thumbnail_len;
				frame_stream.thumbnail_data = thumbnail_data;
				CVI_LOGI("thumb by main video iskey:%d\n", iskey);
			}
		}

		frame_stream.num = stream->u32PackCount;
		if (type == MUXER_FRAME_TYPE_VIDEO) {
			frame_stream.type = MUXER_FRAME_TYPE_VIDEO;
		} else if (type == MUXER_FRAME_TYPE_SUB_VIDEO){
			 frame_stream.type = MUXER_FRAME_TYPE_SUB_VIDEO;
		}

		if (RECORDER_SendFrame(rs->recorder[0], &frame_stream) == 0 && frame_stream.thumbnail_len > 0) {
			*thumbnail_len = 0;
		}
	} break;

	case RECORD_SERVICE_FILE_TYPE_NONE:
		// do nothing
		break;

	default:
		CVI_LOG_ASSERT(0, "Unsupport record format %d", p->recorder_file_type);
		break;
	}

#ifdef DEUBG_VENC_STREAM_STATS
	uint64_t len = 0;
	for (unsigned i = 0; i < stream->u32PackCount; i++) {
		VENC_PACK_S *ppack;
		ppack = &stream->pstPack[i];
		len += ppack->u32Len - ppack->u32Offset;
	}

	rs->venc_rec_stream_count++;
	rs->venc_rec_stream_total_len += len;

	if (rs->venc_rec_stream_count >= DEBUG_VENC_STREAM_STATS_COUNT) {
		CVI_LOGI("RS[%d]: [STREAM_SIZE][REC] cnt %" PRIu64 ", total %" PRIu64 ", bitrate %2.2f bps", rs->id,
				 rs->venc_rec_stream_count, rs->venc_rec_stream_total_len,
				 (float)rs->venc_rec_stream_total_len / rs->venc_rec_stream_count * p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.fFrameRate * 8);
		rs->venc_rec_stream_total_len = 0;
		rs->venc_rec_stream_count = 0;
	}
#endif

	RS_PERF_STAT_ADD(rs->perf_stat_rec_save);

	return RS_SUCCESS;
}

static int32_t recorder_request_idr_cb(void *param, MUXER_FRAME_TYPE_E type)
{
	rs_context_handle_t rs = (rs_context_handle_t)param;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	CVI_LOGI("RS[%d] recorder_request_idr_cb request idr for new video\n", rs->id);
	if(type == MUXER_FRAME_TYPE_VIDEO){
        MAPI_VENC_RequestIDR(p->handles.rec_venc_hdl);
    } else if(type == MUXER_FRAME_TYPE_SUB_VIDEO){
        MAPI_VENC_RequestIDR(p->handles.sub_rec_venc_hdl);
    } else {
        CVI_LOGE("unknown error require IDR frame.");
    }
	rs->need_thumbnail |= 0x2;
	return 0;
}

static int32_t recorder_get_filename_cb(void *param, char *filename, int32_t filename_len)
{
	recorder_get_filename_param_t *rec_param = (recorder_get_filename_param_t *)param;
	if (rec_param == NULL || rec_param->rs == NULL || rec_param->rs->attr == NULL) {
		return 0;
	}
	REC_ATTR_T *p = (REC_ATTR_T *)rec_param->rs->attr;
	char azFilePath[DTCF_PATH_MAX_LEN] = {0};
	memset(azFilePath, 0x0, sizeof(char) * DTCF_PATH_MAX_LEN);

	int32_t ret = ((GENERATE_FILENAME_CB_FN_PTR)p->stCallback.pfnRequestFileNames)(rec_param->rs->id, rec_param->dir_type, FILE_TYPE_SUFFIX[rec_param->file_type], azFilePath, DTCF_PATH_MAX_LEN);
	if (ret != 0) {
		CVI_LOGE("Failed to get filename");
		return -1;
	}
	rec_param->rs->need_thumbnail |= 0x2;
	snprintf(filename, filename_len, "%s", azFilePath);
	return 0;
}

static int32_t mem_buffer_stop_callback(void *param)
{
	rs_context_handle_t rs = (rs_context_handle_t)param;

	pthread_mutex_lock(&rs->state_mutex);
	rs->cur_state = rs->cur_state & (~RS_STATE_MEM_RECORD_EN);
	rs->new_state = rs->new_state & (~RS_STATE_MEM_RECORD_EN);
	pthread_mutex_unlock(&rs->state_mutex);

	CVI_LOGD("RS[%d]: RS_STATE: disable memory recording done", rs->id);

	return 0;
}

static int32_t reset_vproc_bind_state(rs_context_handle_t rs, int32_t bind)
{
	MAPI_VENC_CHN_ATTR_T venc_attr = {0};
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

#ifndef SERVICES_SUBVIDEO_ON
    int32_t sub_vpss_grp = (p->enable_subvideo == 1 ? MAPI_VPROC_GetGrp(p->handles.sub_rec_vproc) : 0);
#endif

	int32_t vpss_grp = MAPI_VPROC_GetGrp(p->handles.rec_vproc);

	MAPI_VENC_GetAttr(p->handles.rec_venc_hdl, &venc_attr);

	if (bind == 0 && p->handles.venc_bind_mode == RECORD_SERVICE_VENC_BIND_MODE_VPSS) {
		if (!venc_attr.sbm_enable) {
			MAPI_VENC_UnBindVproc(p->handles.rec_venc_hdl, vpss_grp, p->handles.vproc_chn_id_venc);
		}

#ifndef SERVICES_SUBVIDEO_ON
        if(p->enable_subvideo == 1){
            MAPI_VENC_UnBindVproc(p->handles.sub_rec_venc_hdl, sub_vpss_grp, p->handles.sub_vproc_chn_id_venc);
        }
#endif
	}

	CVI_LOGD("RS[%d]: bind mode from %d to %d", rs->id, p->handles.venc_bind_mode, bind);
	if (p->handles.rec_venc_hdl != NULL) {
		if (p->handles.venc_rec_start == 1) {
			p->handles.venc_rec_start = 0;
			if (!venc_attr.sbm_enable) {
				MAPI_VENC_StopRecvFrame(p->handles.rec_venc_hdl);
			}
#ifndef SERVICES_SUBVIDEO_ON
            if(p->enable_subvideo == 1){
                MAPI_VENC_StopRecvFrame(p->handles.sub_rec_venc_hdl);
            }
#endif
		}
	}

	if (bind == 1) {
		if (!venc_attr.sbm_enable) {
			MAPI_VENC_BindVproc(p->handles.rec_venc_hdl, vpss_grp, p->handles.vproc_chn_id_venc);
		}
	}

	p->handles.venc_bind_mode = bind;
	return RS_SUCCESS;
}

static int32_t rec_stop_all_callback(void *param)
{
	rs_context_handle_t rs = (rs_context_handle_t)param;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	MAPI_VENC_CHN_ATTR_T venc_attr = {0};

	pthread_mutex_lock(&rs->rec_mutex);

	MAPI_VENC_GetAttr(p->handles.rec_venc_hdl, &venc_attr);

	rs->stop_flag = 1;
	if (p->handles.venc_rec_start == 1) {
		p->handles.venc_rec_start = 0;
		if (!venc_attr.sbm_enable) {
			MAPI_VENC_StopRecvFrame(p->handles.rec_venc_hdl);
		} else {
			MAPI_VPROC_DisableChn(p->handles.rec_vproc, p->handles.vproc_chn_id_venc);
		}
#ifndef SERVICES_SUBVIDEO_ON
        if(p->enable_subvideo == 1){
            MAPI_VENC_StopRecvFrame(p->handles.sub_rec_venc_hdl);
        }
#endif
	}
	reset_vproc_bind_state(rs, 0);
	pthread_mutex_unlock(&rs->rec_mutex);
	return 0;
}

static bool is_ts_file(RECORD_SERVICE_FILE_TYPE_E file_type)
{
	return (RECORD_SERVICE_FILE_TYPE_TS == file_type);
}

static inline int32_t get_video_buffer_size(int32_t bitrate_kbps, int32_t buffer_sec)
{
	// Ratio * (bit rate * buffer duration) + thumbnail size
	return (1.4 * ((bitrate_kbps * buffer_sec) >> 3) + 500);
}

static inline int32_t get_audio_pcm_buffer_size(int32_t sample_rate, int32_t channels, int32_t sample_size, int32_t buffer_sec)
{
	return (sample_rate * channels * sample_size * buffer_sec * 6);
}

#ifdef SERVICES_SUBVIDEO_ON
static void process_recorder_subvideo_stream(const VENC_STREAM_S *stream, void *arg) {
    rs_context_handle_t rs = (rs_context_handle_t)arg;

    REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
    uint8_t *thumbnail_data = (uint8_t *)g_rec_thumbnail_buf[rs->id].buf;
    uint32_t *thumbnail_len = &g_rec_thumbnail_buf[rs->id].actsize;
    MAPI_VENC_HANDLE_T rec_venc_hdl = p->handles.sub_rec_venc_hdl;

    RS_PERF_STAT_INIT();

    switch (p->recorder_file_type) {
        case RECORD_SERVICE_FILE_TYPE_ES:
            if (RS_STATE_ENABLED(rs, RECORD) && (rs->outfp != NULL)) {
                for (uint32_t i = 0; i < stream->u32PackCount; i++) {
                    VENC_PACK_S *ppack;
                    ppack = &stream->pstPack[i];
                    if( ppack->u32Len - ppack->u32Offset > 0){
                        fwrite(ppack->pu8Addr + ppack->u32Offset, ppack->u32Len - ppack->u32Offset, 1, rs->outfp);
                    }
                }
            }
            break;

        case RECORD_SERVICE_FILE_TYPE_MP4:
        case RECORD_SERVICE_FILE_TYPE_MOV:
        case RECORD_SERVICE_FILE_TYPE_TS: {
            RECORDER_FRAME_STREAM_S frame_stream;
            memset(&frame_stream, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
            bool iskey = 0;
            for (unsigned i = 0; i < stream->u32PackCount; i++) {
                VENC_PACK_S *ppack;
                ppack = &stream->pstPack[i];
                frame_stream.data[i] = ppack->pu8Addr + ppack->u32Offset;
                frame_stream.len[i] = ppack->u32Len - ppack->u32Offset;
                frame_stream.vi_pts[i] = stream->pstPack[i].u64PTS;
                MAPI_VENC_GetStreamStatus(rec_venc_hdl, ppack, &iskey);
                frame_stream.vftype[i] = iskey;
                if (p->enable_thumbnail && (rs->need_thumbnail & 0x2) != 0x2 && *thumbnail_len > 0) {
                    frame_stream.thumbnail_len = *thumbnail_len;
                    frame_stream.thumbnail_data = thumbnail_data;
                    CVI_LOGI("thumb by sub video iskey:%d\n", iskey);
                }
            }

            frame_stream.num = stream->u32PackCount;
            frame_stream.type = MUXER_FRAME_TYPE_SUB_VIDEO;
            if (RECORDER_SendFrame(rs->recorder[0], &frame_stream) == 0 && frame_stream.thumbnail_len > 0) {
                *thumbnail_len = 0;
            }
        } break;

        case RECORD_SERVICE_FILE_TYPE_NONE:
            // do nothing
            break;

        default:
            CVI_LOG_ASSERT(0, "Unsupport record format %d", p->recorder_file_type);
            break;
        }

#if 0
    uint64_t len = 0;
    for (unsigned i = 0; i < stream->u32PackCount; i++) {
        VENC_PACK_S *ppack;
        ppack = &stream->pstPack[i];
        len += ppack->u32Len - ppack->u32Offset;
    }

    rs->venc_rec_stream_count++;
    rs->venc_rec_stream_total_len += len;

    if (rs->venc_rec_stream_count >= DEBUG_VENC_STREAM_STATS_COUNT) {
        CVI_LOGI("RS[%d]: [STREAM_SIZE][REC] cnt %"PRIu64", total %"PRIu64", bitrate %2.2f bps", rs->id,
                 rs->venc_rec_stream_count, rs->venc_rec_stream_total_len,
                 (float)rs->venc_rec_stream_total_len / rs->venc_rec_stream_count * p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.fFrameRate * 8);
        rs->venc_rec_stream_total_len = 0;
        rs->venc_rec_stream_count = 0;
    }
#endif
    RS_PERF_STAT_ADD(rs->perf_stat_rec_save);
}
#endif

static void process_recorder_audio_frame(const AUDIO_FRAME_S *audio_frame, const AEC_FRAME_S *aec_frame,
										 void *arg)
{
	UNUSED(aec_frame);
	RECORDER_HANDLE_T recorder = (RECORDER_HANDLE_T)arg;

	if (!recorder) {
		return;
	}

	RECORDER_FRAME_STREAM_S frame;
	memset(&frame, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
	frame.type = MUXER_FRAME_TYPE_AUDIO;
	frame.num = 1;
	frame.data[0] = audio_frame->u64VirAddr[0];
	frame.len[0] = audio_frame->u32Len * 2;
	frame.vi_pts[0] = audio_frame->u64TimeStamp;

	RECORDER_SendFrame(recorder, &frame);
}

static void process_recorder_mute_audio_frame(const AUDIO_FRAME_S *audio_frame, const AEC_FRAME_S *aec_frame,
											  void *arg)
{
	UNUSED(aec_frame);
	RECORDER_HANDLE_T recorder = (RECORDER_HANDLE_T)arg;

	if (!recorder) {
		return;
	}

	static uint8_t *mute_data = NULL;
	if (!mute_data) {
		mute_data = (uint8_t *)calloc(audio_frame->u32Len * 2, sizeof(uint8_t));
	}

	RECORDER_FRAME_STREAM_S frame;
	memset(&frame, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
	frame.type = MUXER_FRAME_TYPE_AUDIO;
	frame.num = 1;
	frame.data[0] = mute_data;
	frame.len[0] = audio_frame->u32Len * 2;
	frame.vi_pts[0] = audio_frame->u64TimeStamp;
	RECORDER_SendFrame(recorder, &frame);
}

static void process_recorder_aac_frame(const AUDIO_STREAM_S *stream, void *arg)
{
	RECORDER_HANDLE_T recorder = (RECORDER_HANDLE_T)arg;

	if (!recorder) {
		return;
	}

	RECORDER_FRAME_STREAM_S frame;
	memset(&frame, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
	frame.type = MUXER_FRAME_TYPE_AUDIO;
	frame.num = 1;
	frame.data[0] = stream->pStream;
	frame.len[0] = stream->u32Len;
	frame.vi_pts[0] = stream->u64TimeStamp;
	RECORDER_SendFrame(recorder, &frame);
}

static void process_recorder_mute_aac_frame(const AUDIO_STREAM_S *stream, void *arg)
{
	RECORDER_HANDLE_T recorder = (RECORDER_HANDLE_T)arg;

	if (!recorder) {
		return;
	}

	static uint8_t *mute_data = NULL;
	static size_t mute_data_size = 0;
	static const size_t adts_header_size = 28;

	if (!mute_data) {
		mute_data_size = stream->u32Len;
		mute_data = (uint8_t *)calloc(mute_data_size, sizeof(uint8_t));

		if (mute_data_size >= adts_header_size) {
			memcpy(mute_data, stream->pStream, adts_header_size);
		} else {
			CVI_LOGI("Init AAC mute date failed");
		}
	}

	RECORDER_FRAME_STREAM_S frame;
	memset(&frame, 0x0, sizeof(RECORDER_FRAME_STREAM_S));
	frame.type = MUXER_FRAME_TYPE_AUDIO;
	frame.num = 1;
	frame.data[0] = mute_data;
	frame.len[0] = mute_data_size;
	frame.vi_pts[0] = stream->u64TimeStamp;
	RECORDER_SendFrame(recorder, &frame);
}

static int32_t start_perf(rs_context_handle_t rs)
{
	char name[32];

#define PRINT_INTERVAL (1000)

	snprintf(name, 32, "FRAME INTERVAL %d", rs->id);
	PERF_MarkInit(&rs->perf_mark_frame, name, PRINT_INTERVAL, 10); // skip 10 frames
	snprintf(name, 32, "  VCAP      %d", rs->id);
	PERF_StatInit(&rs->perf_stat_vcap, name, PRINT_INTERVAL);
	snprintf(name, 32, "  VENC_REC  %d", rs->id);
	PERF_StatInit(&rs->perf_stat_venc_rec, name, PRINT_INTERVAL);
	snprintf(name, 32, "  REC_SAVE  %d", rs->id);
	PERF_StatInit(&rs->perf_stat_rec_save, name, PRINT_INTERVAL);
	snprintf(name, 32, "  VPROC     %d", rs->id);
	PERF_StatInit(&rs->perf_stat_vproc, name, PRINT_INTERVAL);

	return RS_SUCCESS;
}

static int32_t stop_perf(rs_context_handle_t rs)
{
	PERF_MarkDeinit(rs->perf_mark_frame);
	PERF_StatDeinit(rs->perf_stat_vcap);
	PERF_StatDeinit(rs->perf_stat_venc_rec);
	PERF_StatDeinit(rs->perf_stat_rec_save);
	PERF_StatDeinit(rs->perf_stat_vproc);
	return RS_SUCCESS;
}

#ifdef ENABLE_SNAP_ON
static int32_t rs_write_snap_file(char *filename, uint8_t *pivdata, uint32_t pivlen, uint8_t *thmbdata, uint32_t thmblen, uint32_t alignsize)
{
	char JPEG_SOI[2] = {0xFF, 0xD8};
	int32_t fd = open(filename, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
	if (fd < 0) {
		CVI_LOGE("open file fail, %s", filename);
		return -1;
	}

	if (0 > write(fd, JPEG_SOI, sizeof(JPEG_SOI))) {
		CVI_LOGE("write error");
		close(fd);
		return -1;
	}

	if (thmblen < 0xFFFF && thmblen > 0) {
		char JFXX_header[10] = {
			// 10 = 2+2+5+1
			0xFF, 0xE0,								  // APP0 marker
			(thmblen + 8) >> 8, (thmblen + 8) & 0xFF, // Length of segment excluding APP0 marker
			0x4A, 0x46, 0x58, 0x58, 0x00,			  // Identifier,
			0x10									  // Thumbnail format, 1 means jpeg
		};

		if (0 > write(fd, JFXX_header, sizeof(JFXX_header))) {
			CVI_LOGE("write JFXX_header error");
			close(fd);
			return -1;
		}

		if (0 > write(fd, thmbdata, thmblen)) {
			CVI_LOGE("write thumbnail_data error");
			close(fd);
			return -1;
		}
	}
	CVI_LOGD("%d %X\n", pivlen, (pivlen >> 24) & 0xFF);
	unsigned char JPEG_LEN[8] = {0xFF, 0xE2, 0x00, 0x06,
								 (pivlen >> 24) & 0xFF, (pivlen >> 16) & 0xFF,
								 (pivlen >> 8) & 0xFF, (pivlen >> 0) & 0xFF};
	if (0 > write(fd, JPEG_LEN, sizeof(JPEG_LEN))) {
		CVI_LOGE("write piv data end error");
		close(fd);
		return -1;
	}

	// skip SOI, 0xFF, 0xD8
	if (0 > write(fd, pivdata + 2, pivlen - 2)) {
		CVI_LOGE("write piv data error");
		close(fd);
		return -1;
	}

	// add useless msg in file
	char JPEG_END[2] = {0xFF, 0xD9};
	if (0 > write(fd, JPEG_END, sizeof(JPEG_END))) {
		CVI_LOGE("write piv data end error");
		close(fd);
		return -1;
	}

	if (pivlen + 8 < alignsize) {
		ftruncate(fd, alignsize);
	}
	close(fd);
	return 0;
}
#endif

static void rs_thumb_task_entry(void *arg)
{
	rs_context_handle_t rs = (rs_context_handle_t)arg;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	VIDEO_FRAME_INFO_S frame = {0};
	VENC_STREAM_S stream = {0};
	uint8_t *thumbdata = NULL;
	uint32_t *thmbsize = 0;
	uint32_t bufsize = 0;
	int32_t thumbnail_flag = 0;
	bool thumb_chn_enabled = false;
	while (!rs->shutdown) {
		if (p->enable_thumbnail == false || rs->need_thumbnail == 0) {
			OSAL_TASK_Sleep(10 * 1000);
			continue;
		}

		pthread_mutex_lock(&rs->thumbnail_mutex);
		if ((rs->need_thumbnail & 0x1) == 1) {
#ifdef ENABLE_SNAP_ON
			thumbdata = (uint8_t *)g_snap_thumbnail_buf[rs->id].buf;
			thmbsize = &g_snap_thumbnail_buf[rs->id].actsize;
			bufsize = g_snap_thumbnail_buf[rs->id].size;
#endif
			thumbnail_flag = 0;
		} else if ((rs->need_thumbnail & 0x2) == 0x2) {
			thumbdata = (uint8_t *)g_rec_thumbnail_buf[rs->id].buf;
			thmbsize = &g_rec_thumbnail_buf[rs->id].actsize;
			bufsize = g_rec_thumbnail_buf[rs->id].size;
			thumbnail_flag = 1;
		}
		*thmbsize = 0;

		if (rs_thumbnail_chn_can_be_toggled(p)) {
			if (MAPI_VPROC_EnableChn(p->handles.thumbnail_vproc, p->handles.vproc_chn_id_thumbnail) == 0) {
				thumb_chn_enabled = true;
				OSAL_TASK_Sleep(rs_thumb_settle_delay_us(p));
			} else {
				CVI_LOGE("RS[%d]: MAPI_VPROC_EnableChn(thumbnail) failed", rs->id);
				goto END1;
			}
		}

		MAPI_VENC_StartRecvFrame(p->handles.thumbnail_venc_hdl, -1);

		/* Drop a few frames: first outputs after enable often mismatch 4K pipeline timing. */
		if (thumb_chn_enabled) {
			int32_t warmup_frames = rs_rec_main_video_width(p) >= 3840 ? 3 : 2;

			for (int32_t w = 0; w < warmup_frames; w++) {
				int32_t wret = -1;

				for (int32_t attempt = 0; attempt < 8; attempt++) {
					wret = MAPI_VPROC_GetChnFrame(p->handles.thumbnail_vproc, p->handles.vproc_chn_id_thumbnail,
								      &frame);
					if (wret == 0) {
						break;
					}
					OSAL_TASK_Sleep(20 * 1000);
				}
				if (wret == 0) {
					LOG_RET(MAPI_ReleaseFrame(&frame));
				}
			}
		}

		int32_t ret = -1;

		for (int32_t attempt = 0; attempt < 8; attempt++) {
			ret = MAPI_VPROC_GetChnFrame(p->handles.thumbnail_vproc, p->handles.vproc_chn_id_thumbnail, &frame);
			if (ret == 0) {
				break;
			}
			OSAL_TASK_Sleep(20 * 1000);
		}
		if (ret != 0) {
			CVI_LOGE("RS[%d]: MAPI_VPROC_GetChnFrame failed", rs->id);
			goto END1;
		}

		ret = MAPI_VENC_SendFrame(p->handles.thumbnail_venc_hdl, &frame);
		if (ret != 0) {
			CVI_LOGE("RS[%d]: MAPI_VENC_SendFrame failed", rs->id);
			goto END;
		}

		ret = MAPI_VENC_GetStreamTimeWait(p->handles.thumbnail_venc_hdl, &stream, 3000);
		if (ret != 0) {
			CVI_LOGE("RS[%d]: MAPI_VENC_GetStreamTimeWait failed", rs->id);
			goto END;
		}
		if (stream.u32PackCount <= 0 || stream.u32PackCount > FRAME_STREAM_SEGMENT_MAX_NUM) {
			CVI_LOGE("RS[%d]: thumbnail stream pack count %u invalid", rs->id, stream.u32PackCount);
			LOG_RET(MAPI_VENC_ReleaseStream(p->handles.thumbnail_venc_hdl, &stream));
			goto END;
		}

		CVI_LOGD("RS[%d]: thumbnail buf size %u thmbsize %u\n", rs->id, bufsize, stream.pstPack[0].u32Len);
		if (bufsize < stream.pstPack[0].u32Len) {
			LOG_RET(MAPI_VENC_ReleaseStream(p->handles.thumbnail_venc_hdl, &stream));
			goto END;
		}

		memcpy(thumbdata, stream.pstPack[0].pu8Addr, stream.pstPack[0].u32Len);
		*thmbsize = stream.pstPack[0].u32Len;
		LOG_RET(MAPI_VENC_ReleaseStream(p->handles.thumbnail_venc_hdl, &stream));
	END:
		LOG_RET(MAPI_ReleaseFrame(&frame));
	END1:
		MAPI_VENC_StopRecvFrame(p->handles.thumbnail_venc_hdl);
		if (thumb_chn_enabled) {
			LOG_RET(MAPI_VPROC_DisableChn(p->handles.thumbnail_vproc, p->handles.vproc_chn_id_thumbnail));
			thumb_chn_enabled = false;
		}
		if (thumbnail_flag == 0) {
			rs->need_thumbnail &= (~0x1);
		} else if (thumbnail_flag == 1) {
			rs->need_thumbnail &= (~0x2);
		}
		CVI_LOGI("RS[%d] catch thumbnail %s\n", rs->id, (*thmbsize > 0) ? ("success") : ("failed"));
		pthread_mutex_unlock(&rs->thumbnail_mutex);
		OSAL_TASK_Sleep(10 * 1000);
	}
	CVI_LOGI("RS[%d] exit\n", rs->id);
}

#ifdef ENABLE_SNAP_ON

static void rs_snap_task_entry(void *arg)
{
	rs_context_handle_t rs = (rs_context_handle_t)arg;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	uint8_t *thumbnail_data = (uint8_t *)g_snap_thumbnail_buf[rs->id].buf;
	uint32_t *thumbnail_len = &g_snap_thumbnail_buf[rs->id].actsize;
	VIDEO_FRAME_INFO_S piv_frame;
	VENC_STREAM_S stream = {0};

	if (thumbnail_data == NULL) {
		CVI_LOGI("Currently may support photo taking mode %d", rs->id);
		return;
	}

	while (!rs->shutdown) {
		OSAL_TASK_Sleep(50 * 1000);
		pthread_mutex_lock(&rs->piv_mutex);
		rs->piv_shared = 1;
		pthread_cond_wait(&rs->piv_cond, &rs->piv_mutex);
		if (rs->shutdown == 1) {
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			break;
		}

		if (p->stCallback.pfnNormalRecCb) {
			((RECORDER_EVENT_CALLBACK)p->stCallback.pfnNormalRecCb)(RECORDER_EVENT_PIV_START, rs->piv_filename, (void *)(&rs->id));
		}

		if (p->handles.piv_venc_hdl == NULL) {
			CVI_LOGE("null snapshot venc hdl");
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}

		if (p->enable_thumbnail) {
			*thumbnail_len = 0;
			rs->need_thumbnail |= 0x1;
			int32_t timeout_cnt = 0;
			while (*thumbnail_len == 0) {
				if (rs->shutdown == 1 || timeout_cnt++ > 20) {
					rs->piv_finish = 1;
					pthread_mutex_unlock(&rs->piv_mutex);
					break;
				}
				OSAL_TASK_Sleep(10 * 1000);
			}

			if (*thumbnail_len == 0) {
				rs->piv_finish = 1;
				pthread_mutex_unlock(&rs->piv_mutex);
				continue;
			}
		}

		MAPI_VENC_StartRecvFrame(p->handles.piv_venc_hdl, -1);

		if (0 != MAPI_VPROC_GetChnFrame(p->handles.rec_vproc, p->handles.vproc_chn_id_venc, &piv_frame)) {
			CVI_LOGE("RS[%d]: MAPI_VPROC_GetChnFrame failed", rs->id);
			MAPI_VENC_StopRecvFrame(p->handles.piv_venc_hdl);
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}

		if (0 > MAPI_VENC_SendFrame(p->handles.piv_venc_hdl, &piv_frame)) {
			CVI_LOGE("RS[%d]: snapshot venc send frame fail", rs->id);
			MAPI_ReleaseFrame(&piv_frame);
			MAPI_VENC_StopRecvFrame(p->handles.piv_venc_hdl);
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}

		if (0 > rs_get_venc_stream(p->handles.piv_venc_hdl, &stream)) {
			CVI_LOGE("RS[%d]: snapshot get venc stream fail", rs->id);
			MAPI_ReleaseFrame(&piv_frame);
			MAPI_VENC_StopRecvFrame(p->handles.piv_venc_hdl);
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}

		MAPI_ReleaseFrame(&piv_frame);
		uint32_t len = stream.pstPack[0].u32Len;

		if (len > g_snap_buf[rs->id].size) {
			CVI_LOGE("RS[%d]snapshot size too big %u %u", rs->id, len, g_snap_buf[rs->id].size);
			MAPI_VENC_ReleaseStream(p->handles.piv_venc_hdl, &stream);
			MAPI_VENC_StopRecvFrame(p->handles.piv_venc_hdl);
			rs->piv_finish = 1;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}
		memcpy(g_snap_buf[rs->id].buf, stream.pstPack[0].pu8Addr, len);

		MAPI_VENC_ReleaseStream(p->handles.piv_venc_hdl, &stream);
		MAPI_VENC_StopRecvFrame(p->handles.piv_venc_hdl);

		if (rs_write_snap_file(rs->piv_filename, g_snap_buf[rs->id].buf, len, thumbnail_data, *thumbnail_len, rs->piv_prealloclen) < 0) {
			rs->piv_finish = 1;
			*thumbnail_len = 0;
			pthread_mutex_unlock(&rs->piv_mutex);
			continue;
		}

		if (p->stCallback.pfnNormalRecCb) {
			((RECORDER_EVENT_CALLBACK)p->stCallback.pfnNormalRecCb)(RECORDER_EVENT_PIV_END, rs->piv_filename, (void *)(&rs->id));
		}

		// zbar_processor_t *proc = zbar_processor_create(0);
		CVI_LOGD("RS[%d] snap success", rs->id);
		FILESYNC_Push(rs->piv_filename, NULL);
		rs->piv_finish = 1;
		*thumbnail_len = 0;
		pthread_mutex_unlock(&rs->piv_mutex);
	}
	CVI_LOGD("RS[%d] exit", rs->id);
}
#endif

static int32_t process_one_frame(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	VENC_STREAM_S stream = {0};
	int32_t ret = 0;

	pthread_mutex_lock(&rs->rec_mutex);
	if (p->handles.venc_rec_start == 0) {
		pthread_mutex_unlock(&rs->rec_mutex);
		return 0;
	}

	ret = rs_get_venc_stream(p->handles.rec_venc_hdl, &stream);
	if (ret != 0) {
		CVI_LOGE("RS[%d] rs_get_venc_stream failed\n", rs->id);
		pthread_mutex_unlock(&rs->rec_mutex);
		return -1;
	}

	LOG_RET(process_venc_rec_stream(rs, p->handles.rec_venc_hdl, MUXER_FRAME_TYPE_VIDEO, &stream));
	LOG_RET(MAPI_VENC_ReleaseStream(p->handles.rec_venc_hdl, &stream));
	pthread_mutex_unlock(&rs->rec_mutex);

	return 0;
}

// #ifndef SERVICES_SUBVIDEO_ON
// static int32_t process_one_subframe(rs_context_handle_t rs) {
//     REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
//     VENC_STREAM_S sub_stream = {0};
//     int32_t ret = 0;

//     pthread_mutex_lock(&rs->rec_mutex);
//     if (p->handles.venc_rec_start == 0) {
//         pthread_mutex_unlock(&rs->rec_mutex);
//         return 0;
//     }
//     ret = rs_get_venc_stream(p->handles.sub_rec_venc_hdl, &sub_stream);
//     if (ret != 0) {
//         CVI_LOGE("RS[%d] rs_get_venc_stream failed\n", rs->id);
//         pthread_mutex_unlock(&rs->rec_mutex);
//         return -1;
//     }

//     LOG_RET(process_venc_rec_stream(rs, p->handles.sub_rec_venc_hdl, MUXER_FRAME_TYPE_SUB_VIDEO,&sub_stream));
//     LOG_RET(MAPI_VENC_ReleaseStream(p->handles.sub_rec_venc_hdl, &sub_stream));
//     pthread_mutex_unlock(&rs->rec_mutex);

//     return 0;
// }
// #endif

static int32_t process_one_subframe_timelapse(rs_context_handle_t rs)
{
    REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

    VIDEO_FRAME_INFO_S vproc_frame;
    VENC_STREAM_S stream = {0};

    int32_t ret = 0;
    pthread_mutex_lock(&rs->rec_mutex);
    if (p->handles.venc_rec_start == 0) {
        pthread_mutex_unlock(&rs->rec_mutex);
        return 0;
    }

    if(RECORDER_Timelapse_Is_SendVenc(rs->recorder[0], MUXER_FRAME_TYPE_SUB_VIDEO) != 1){
        pthread_mutex_unlock(&rs->rec_mutex);
        return 0;
    }

    ret = MAPI_VPROC_GetChnFrame(p->handles.sub_rec_vproc, p->handles.sub_vproc_chn_id_venc, &vproc_frame);
    if (ret != 0) {
        CVI_LOGE("RS[%d]: MAPI_VPROC_GetChnFrame failed", rs->id);
        pthread_mutex_unlock(&rs->rec_mutex);
        return -1;
    }

    ret = MAPI_VENC_SendFrame(p->handles.sub_rec_venc_hdl, &vproc_frame);
    if (ret < 0) {
        CVI_LOGE("RS[%d]: MAPI_VENC_SendFrame failed", rs->id);
        LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
        pthread_mutex_unlock(&rs->rec_mutex);
        return -1;
    }

    if (rs_get_venc_stream(p->handles.sub_rec_venc_hdl, &stream) != 0) {
        LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
        pthread_mutex_unlock(&rs->rec_mutex);
        return -1;
    }

    if (p->handles.venc_bind_mode == RECORD_SERVICE_VENC_BIND_MODE_NONE) {
        LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
    }
    LOG_RET(process_venc_rec_stream(rs, p->handles.sub_rec_venc_hdl, MUXER_FRAME_TYPE_SUB_VIDEO, &stream));
    LOG_RET(MAPI_VENC_ReleaseStream(p->handles.sub_rec_venc_hdl, &stream));

    pthread_mutex_unlock(&rs->rec_mutex);
    return 1;
}

static int32_t process_one_frame_timelapse(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	VIDEO_FRAME_INFO_S vproc_frame;
	VENC_STREAM_S stream = {0};

	int32_t ret = 0;
	pthread_mutex_lock(&rs->rec_mutex);
	if (p->handles.venc_rec_start == 0) {
		pthread_mutex_unlock(&rs->rec_mutex);
		return 0;
	}

	if (RECORDER_Timelapse_Is_SendVenc(rs->recorder[0], MUXER_FRAME_TYPE_VIDEO) != 1) {
		pthread_mutex_unlock(&rs->rec_mutex);
		return 0;
	}

	ret = MAPI_VPROC_GetChnFrame(p->handles.rec_vproc, p->handles.vproc_chn_id_venc, &vproc_frame);
	if (ret != 0) {
		CVI_LOGE("RS[%d]: MAPI_VPROC_GetChnFrame failed", rs->id);
		pthread_mutex_unlock(&rs->rec_mutex);
		return -1;
	}

	ret = MAPI_VENC_SendFrame(p->handles.rec_venc_hdl, &vproc_frame);
	if (ret < 0) {
		CVI_LOGE("RS[%d]: MAPI_VENC_SendFrame failed", rs->id);
		LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
		pthread_mutex_unlock(&rs->rec_mutex);
		return -1;
	}

	if (rs_get_venc_stream(p->handles.rec_venc_hdl, &stream) != 0) {
		LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
		pthread_mutex_unlock(&rs->rec_mutex);
		return -1;
	}

	if (p->handles.venc_bind_mode == RECORD_SERVICE_VENC_BIND_MODE_NONE) {
		LOG_RET(MAPI_ReleaseFrame(&vproc_frame));
	}

	LOG_RET(process_venc_rec_stream(rs, p->handles.rec_venc_hdl, MUXER_FRAME_TYPE_VIDEO, &stream));
	LOG_RET(MAPI_VENC_ReleaseStream(p->handles.rec_venc_hdl, &stream));

	pthread_mutex_unlock(&rs->rec_mutex);
	return 1;
}

static void fill_rec_audio_callback_name(char *name, int32_t size, int32_t id0, int32_t id1)
{
	snprintf(name, size, "recorder_%d%d", id0, id1);
}

static int32_t add_audio_mute_callback_to_recorder(rs_context_handle_t rs)
{
	char name[64] = {0};

	if (rs->recorder[0]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 0);
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackSet(name, process_recorder_mute_audio_frame, rs->recorder[0]));
	}

	if (rs->recorder[1]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 1);
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackSet(name, process_recorder_mute_audio_frame, rs->recorder[1]));
	}
	return RS_SUCCESS;
}

static int32_t add_audio_callback_to_recorder(rs_context_handle_t rs)
{
	char name[64] = {0};

	if (rs->recorder[0]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 0);
		CVI_LOGD("name %s", name);
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackSet(name, process_recorder_audio_frame, rs->recorder[0]));
	}

	if (rs->recorder[1]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 1);
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackSet(name, process_recorder_audio_frame, rs->recorder[1]));
	}
	return RS_SUCCESS;
}

#ifdef SERVICES_SUBVIDEO_ON
static int32_t add_subvideo_callback_to_recorder(rs_context_handle_t rs) {
    char name[64] = "recorder_subvideo";
    sprintf(name, "recorder_subvideo_%d", rs->id);
    REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

    if (rs->recorder[0]) {
        CVI_LOGI("recorder add name %s subvideo cb", name);
        LOG_RET(VIDEO_SERVICR_CallbackUnSet(rs->id, name));
        LOG_RET(VIDEO_SERVICR_CallbackSet(rs->id, name, process_recorder_subvideo_stream, rs));
        MAPI_VENC_RequestIDR(p->handles.sub_rec_venc_hdl);/*请求I帧*/
    }

    return RS_SUCCESS;
}

static int32_t remove_subvideo_callback_to_recorder(rs_context_handle_t rs) {
    char name[64] = "recorder_subvideo";
    sprintf(name, "recorder_subvideo_%d", rs->id);

    if (rs->recorder[0]) {
        LOG_RET(VIDEO_SERVICR_CallbackUnSet(rs->id, name));
    }

    return RS_SUCCESS;
}
#endif

static int32_t add_audio_aac_mute_callback_to_recorder(rs_context_handle_t rs)
{
	char name[64] = {0};

	if (rs->recorder[0]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 0);
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackSet(name, process_recorder_mute_aac_frame, rs->recorder[0]));
	}

	if (rs->recorder[1]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 1);
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackSet(name, process_recorder_mute_aac_frame, rs->recorder[1]));
	}
	return RS_SUCCESS;
}

static int32_t add_audio_aac_callback_to_recorder(rs_context_handle_t rs)
{
	char name[64] = {0};

	if (rs->recorder[0]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 0);
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackSet(name, process_recorder_aac_frame, rs->recorder[0]));
	}

	if (rs->recorder[1]) {
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 1);
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackUnset(name));
		LOG_RET(AUDIO_SERVICR_ACAP_AacCallbackSet(name, process_recorder_aac_frame, rs->recorder[1]));
	}
	return RS_SUCCESS;
}

static void master_update_audio_callback(rs_context_handle_t rs, bool is_mute)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	RECORDER_TRACK_SOURCE_S *handle = &p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO];
	CVI_LOGD("RS[%d]: Start audio %d enable %d", rs->id, is_mute, handle->enable);
	if (p->enRecType == RECORDER_TYPE_LAPSE) {
		return;
	}
	if (1 || MUXER_TRACK_AUDIO_CODEC_ADPCM == handle->unTrackSourceAttr.stAudioInfo.enCodecType) {
		if (is_mute) {
			add_audio_mute_callback_to_recorder(rs);
		} else {
			add_audio_callback_to_recorder(rs);
		}
	} else if (MUXER_TRACK_AUDIO_CODEC_AAC == handle->unTrackSourceAttr.stAudioInfo.enCodecType) {
		if (is_mute) {
			add_audio_aac_mute_callback_to_recorder(rs);
		} else {
			add_audio_aac_callback_to_recorder(rs);
		}
	}
}

static int32_t start_mute(rs_context_handle_t rs)
{

	CVI_LOGD("RS[%d]: Start mute", rs->id);

	master_update_audio_callback(rs, true);

	return RS_SUCCESS;
}

static int32_t stop_mute(rs_context_handle_t rs)
{

	CVI_LOGD("RS[%d]: Stop mute", rs->id);

	master_update_audio_callback(rs, false);

	return RS_SUCCESS;
}

static int32_t master_destroy_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	switch (p->recorder_file_type) {
	case RECORD_SERVICE_FILE_TYPE_MP4:
	case RECORD_SERVICE_FILE_TYPE_MOV:
	case RECORD_SERVICE_FILE_TYPE_TS: {
		while (!rs->stop_flag) {
			OSAL_TASK_Sleep(20);
		}
		char name[64] = {0};
		fill_rec_audio_callback_name(name, sizeof(name), rs->id, 0);
		LOG_RET(AUDIO_SERVICR_ACAP_CallbackUnset(name));

		pthread_mutex_lock(&rs->rec_mutex);
		reset_vproc_bind_state(rs, 0);
		pthread_mutex_unlock(&rs->rec_mutex);

		RECORDER_Destroy(&rs->recorder[0]);
		RECORDER_Destroy(&rs->recorder[1]);
		rs->recorder[0] = NULL;
		rs->recorder[1] = NULL;
		break;
	}

	case RECORD_SERVICE_FILE_TYPE_ES:
	case RECORD_SERVICE_FILE_TYPE_NONE:
		break;

	default:
		CVI_LOGE("Unsupported recorder file type %d", p->recorder_file_type);
		return RS_ERR_INVALID;
	}
	CVI_LOGI("RS[%d]: Destroy Recorder End", rs->id);
	return RS_SUCCESS;
}

static int32_t master_create_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	CVI_LOGD("R: Create Recorder");

	switch (p->recorder_file_type) {
	case RECORD_SERVICE_FILE_TYPE_MP4:
	case RECORD_SERVICE_FILE_TYPE_MOV:
	case RECORD_SERVICE_FILE_TYPE_TS: {
		RECORDER_ATTR_S rec_attr;
		memset(&rec_attr, 0x00, sizeof(RECORDER_ATTR_S));
		memcpy(&rec_attr.astStreamAttr, &p->astStreamAttr[0], sizeof(RECORDER_STREAM_ATTR_S));

		rec_attr.enRecType = p->enRecType;
		rec_attr.fncallback.pfn_rec_malloc_mem = MAPI_MemAllocate;
		rec_attr.fncallback.pfn_rec_free_mem = MAPI_MemFree;
		RECORDER_TRACK_SOURCE_S *handle = &p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO];
		{
			RECORDER_TRACK_SOURCE_S *thandle = &rec_attr.astStreamAttr.aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO];
			if (rec_attr.enRecType == RECORDER_TYPE_NORMAL) {
				thandle->enable = 1;
			}
			thandle->unTrackSourceAttr.stAudioInfo.u32ChnCnt = handle->unTrackSourceAttr.stAudioInfo.u32ChnCnt;
			thandle->unTrackSourceAttr.stAudioInfo.u32SampleRate = handle->unTrackSourceAttr.stAudioInfo.u32SampleRate;
			if (MUXER_TRACK_AUDIO_CODEC_ADPCM == handle->unTrackSourceAttr.stAudioInfo.enCodecType) {
				thandle->unTrackSourceAttr.stAudioInfo.fFramerate =
					(float)handle->unTrackSourceAttr.stAudioInfo.u32SampleRate / handle->unTrackSourceAttr.stAudioInfo.u32SamplesPerFrame;
			} else if (MUXER_TRACK_AUDIO_CODEC_AAC == handle->unTrackSourceAttr.stAudioInfo.enCodecType) {
				thandle->unTrackSourceAttr.stAudioInfo.fFramerate =
					(float)handle->unTrackSourceAttr.stAudioInfo.u32SampleRate / handle->unTrackSourceAttr.stAudioInfo.u32SamplesPerFrame;
			}
			CVI_LOGD("audio u32SampleRate %u u32SamplesPerFrame %u", thandle->unTrackSourceAttr.stAudioInfo.u32SampleRate, thandle->unTrackSourceAttr.stAudioInfo.u32SamplesPerFrame);
			CVI_LOGD("audio en %d enRecType %d fFramerate %f", thandle->enable, rec_attr.enRecType, thandle->unTrackSourceAttr.stAudioInfo.fFramerate);
		}

		rec_attr.fncallback.pfn_request_idr = (RECORDER_REQUEST_IDR_CALLBACK)recorder_request_idr_cb;
		rec_attr.fncallback.pfn_request_idr_param = (void *)rs;

		rec_attr.enable_subtitle = is_ts_file(p->recorder_file_type) ? false : p->enable_subtitle;
		if (rec_attr.enable_subtitle) {
			rec_attr.subtitle_framerate = 5;
			rec_attr.fncallback.pfn_get_subtitle_cb = (RECORDER_GET_SUBTITLE_CALLBACK)p->stCallback.pfnGetSubtitleCb;
			rec_attr.fncallback.pfn_get_subtitle_cb_param = (void *)NULL;
		}

		rec_attr.enable_thumbnail = p->enable_thumbnail;

		rec_attr.enable_file_alignment = !is_ts_file(p->recorder_file_type);
#ifdef EVENT_REC_FILE_COPY_FROM_NORM_ON
		rec_attr.enable_emrfile_from_normfile = 1;
#else
		rec_attr.enable_emrfile_from_normfile = 0;
#endif
		rec_attr.fncallback.pfn_get_filename = (RECORDER_GET_FILENAME_CALLBACK)recorder_get_filename_cb;
		recorder_get_filename_param_t *fparam;
		if (rec_attr.enRecType == RECORDER_TYPE_NORMAL) {
			rec_attr.fncallback.pfn_event_cb[RECORDER_TYPE_NORMAL_INDEX] = p->stCallback.pfnNormalRecCb;
			rec_attr.fncallback.pfn_event_cb_param = (void *)&rs->id;
			fparam = (recorder_get_filename_param_t *)malloc(sizeof(recorder_get_filename_param_t));
			fparam->rs = rs;
			fparam->dir_type = p->normal_dir_type[0];
			fparam->file_type = p->recorder_file_type;
			rec_attr.fncallback.pfn_get_filename_param[RECORDER_CALLBACK_TYPE_NORMAL] = (void *)fparam;

			rec_attr.fncallback.pfn_event_cb[RECORDER_TYPE_EVENT_INDEX] = p->stCallback.pfnEventRecCb;
			rec_attr.u32PostRecTimeSec = p->u32PostRecTimeSec;
			rec_attr.u32PreRecTimeSec = p->u32PreRecTimeSec;
			fparam = (recorder_get_filename_param_t *)malloc(sizeof(recorder_get_filename_param_t));
			fparam->rs = rs;
			fparam->dir_type = p->event_dir_type[0];
			fparam->file_type = p->recorder_file_type;
			rec_attr.fncallback.pfn_get_filename_param[RECORDER_CALLBACK_TYPE_EVENT] = (void *)fparam;
		} else if (rec_attr.enRecType == RECORDER_TYPE_LAPSE) {
			rec_attr.fncallback.pfn_event_cb[RECORDER_TYPE_LAPSE_INDEX] = p->stCallback.pfnLapseRecCb;
			rec_attr.fncallback.pfn_event_cb_param = (void *)&rs->id;
			fparam = (recorder_get_filename_param_t *)malloc(sizeof(recorder_get_filename_param_t));
			fparam->rs = rs;
			fparam->dir_type = p->normal_dir_type[0];
			fparam->file_type = p->recorder_file_type;
			rec_attr.fncallback.pfn_get_filename_param[RECORDER_CALLBACK_TYPE_LAPSE] = (void *)fparam;
			rec_attr.unRecAttr.stLapseRecAttr.fFramerate = p->unRecAttr.stLapseRecAttr.fFramerate;
			rec_attr.unRecAttr.stLapseRecAttr.u32IntervalMs = p->unRecAttr.stLapseRecAttr.u32IntervalMs;
		} else {
			return -1;
		}

		rec_attr.device_model = p->devmodel;
		rec_attr.short_file_ms = p->short_file_ms;
		rec_attr.prealloc_size = p->s32RecPresize;
		rec_attr.s32MemRecPreSec = p->s32MemRecPreSec;
		rec_attr.stSplitAttr.enSplitType = p->stSplitAttr.enSplitType;
		rec_attr.stSplitAttr.u64SplitTimeLenMSec = p->stSplitAttr.u64SplitTimeLenMSec;

		rec_attr.fncallback.pfn_mem_buffer_stop_cb = mem_buffer_stop_callback;
		rec_attr.fncallback.pfn_mem_buffer_stop_cb_param = (void *)rs;
		rec_attr.fncallback.pfn_rec_stop_cb = rec_stop_all_callback;
		rec_attr.fncallback.pfn_rec_stop_cb_param = (void *)rs;
		rs->stop_flag = 1;

		int32_t presec = ((rec_attr.u32PreRecTimeSec >= (uint32_t)rec_attr.s32MemRecPreSec) ? rec_attr.u32PreRecTimeSec : (uint32_t)rec_attr.s32MemRecPreSec);
		if (presec == 0 || rec_attr.enRecType == RECORDER_TYPE_LAPSE) {
			presec = 1;
		} else {
			presec += 1;
		}
		uint32_t bitrate = p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.u32BitRate;
		uint32_t sub_bitrate = p->astStreamAttr[0].aHTrackSrcHandle[CVI_RECORDER_TRACK_SOURCE_TYPE_SUB_VIDEO].unTrackSourceAttr.stVideoInfo.u32BitRate;
		uint32_t sampleRate = p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO].unTrackSourceAttr.stAudioInfo.u32SampleRate;
		uint32_t chns = p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO].unTrackSourceAttr.stAudioInfo.u32ChnCnt;
		CVI_LOGD("video bitrate %u audio sampleRate %u chns %u presec %d", bitrate, sampleRate, chns, presec);
		rec_attr.stRbufAttr[RECORDER_RBUF_VIDEO].size = get_video_buffer_size(bitrate, presec) * 1024;
                rec_attr.stRbufAttr[RECORDER_RBUF_VIDEO].name = (const char*)"rs_v";
                if (p->enable_subvideo) {
                    rec_attr.enable_subvideo = p->enable_subvideo;
                    rec_attr.stRbufAttr[RECORDER_RBUF_SUB_VIDEO].size = get_video_buffer_size(sub_bitrate, presec) * 1024;
                    rec_attr.stRbufAttr[RECORDER_RBUF_SUB_VIDEO].name = (const CVI_CHAR*)"rs_sub_v";
                }
                rec_attr.stRbufAttr[RECORDER_RBUF_AUDIO].size = get_audio_pcm_buffer_size(sampleRate, chns, 2, presec);
                rec_attr.stRbufAttr[RECORDER_RBUF_AUDIO].name = (const char*)"rs_a";
                rec_attr.stRbufAttr[RECORDER_RBUF_SUBTITLE].size = 50 * 1024;
                rec_attr.stRbufAttr[RECORDER_RBUF_SUBTITLE].name = (const char *)"rs_s";

		rec_attr.id = rs->id;
		CHECK_RET(RECORDER_Create(&rs->recorder[0], &rec_attr));
		if (rec_attr.enRecType == RECORDER_TYPE_NORMAL) {
			if (p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO].enable == 1) {
				stop_mute(rs);
			} else {
				start_mute(rs);
			}
		}
	} break;

	case RECORD_SERVICE_FILE_TYPE_ES:
	case RECORD_SERVICE_FILE_TYPE_NONE:
		break;

	default:
		CVI_LOGE("Unsupported recorder file type %d", p->recorder_file_type);
		return RS_ERR_INVALID;
	}

	return RS_SUCCESS;
}

#ifdef ENABLE_SUBTITLE_ON
typedef int32_t (*GET_GPSINFO_CB_FN_PTR)(RECORD_SERVICE_GPS_INFO_S* info);
static void master_subtitle_task_entry(void* arg)
{
	rs_context_handle_t rs = (rs_context_handle_t)arg;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	RECORDER_FRAME_STREAM_S frame;
	frame.type = MUXER_FRAME_TYPE_SUBTITLE;
	frame.num = 1;
	frame.thumbnail_data = NULL;
	frame.thumbnail_len = 0;
	GET_GPSINFO_CB_FN_PTR cb = (GET_GPSINFO_CB_FN_PTR)(p->stCallback.pfnGetGPSInfoCb);
	RECORD_SERVICE_GPS_INFO_S info;

#define MUXER_SUBTITLE_MAX_LEN (200)
#define SUBTITLE_GPS_HEAD_LEN (5) /*0'0x0,1'dataLen,2''G',3''P',4''S'*/
#define SUBTITLE_HEAD_LEN (2)	  /*0'0x0,1'dataLen*/

	if (cb) {
		frame.len[0] = sizeof(RECORD_SERVICE_GPS_RMCINFO_S) + SUBTITLE_GPS_HEAD_LEN;
	} else {
		frame.len[0] = MUXER_SUBTITLE_MAX_LEN + SUBTITLE_HEAD_LEN;
	}
	frame.data[0] = (uint8_t *)malloc(frame.len[0]);

	uint64_t subtitle_duration_ms = 200; /*1000 / framerate*/
	uint64_t start = RECORDER_GetUs();
	uint64_t sub_cnt = 0;
	while (!rs->shutdown) {
		if (rs->stop_flag != 0 || !p->enable_subtitle || p->enRecType != RECORDER_TYPE_NORMAL) {
			OSAL_TASK_Sleep(50 * 1000);
			sub_cnt = 0;
			continue;
		}
		uint64_t end = RECORDER_GetUs();
		if (sub_cnt == 0 || (end - start) >= subtitle_duration_ms * 1000 * sub_cnt) {
			if (sub_cnt == 0) {
				start = RECORDER_GetUs();
			}
			memset(frame.data[0], 0, frame.len[0]);
			if (cb) {
				cb(&info);
				frame.data[0][0] = 0x0;
				frame.data[0][1] = sizeof(RECORD_SERVICE_GPS_RMCINFO_S);
				frame.data[0][2] = 'G';
				frame.data[0][3] = 'P';
				frame.data[0][4] = 'S';
				memcpy(frame.data[0] + SUBTITLE_GPS_HEAD_LEN, &info.rmc_info, frame.len[0]);
			} else {
				frame.data[0][0] = 0x0;
				frame.data[0][1] = MUXER_SUBTITLE_MAX_LEN;
				((RECORDER_GET_SUBTITLE_CALLBACK)(p->stCallback.pfnGetSubtitleCb))(NULL, rs->id, (char *)frame.data[0] + SUBTITLE_HEAD_LEN, MUXER_SUBTITLE_MAX_LEN);
			}
			frame.vi_pts[0] = end;
			LOG_RET(RECORDER_SendFrame(rs->recorder[0], &frame));
			sub_cnt++;
		}
		OSAL_TASK_Sleep(20 * 1000);
	}
	free((void*)frame.data[0]);
}
#endif

static void master_video_task_entry(void *arg)
{
	rs_context_handle_t rs = (rs_context_handle_t)arg;
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	uint64_t step0 = 0, step1 = RECORDER_GetUs();
	while (!rs->shutdown) {

		if (rs->stop_flag == 0) {
			step0 = RECORDER_GetUs();
			if (step0 - step1 > 100 * 1000) {
				CVI_LOGW("task[%d] schedule timeout %" PRIu64 "", rs->id, step0 - step1);
			}

			if (p->enRecType == RECORDER_TYPE_NORMAL) {
				process_one_frame(rs);
			} else if (p->enRecType == RECORDER_TYPE_LAPSE) {
				process_one_frame_timelapse(rs);
			}

			step1 = RECORDER_GetUs();
			if (step1 - step0 > 100 * 1000) {
				CVI_LOGW("task[%d] get && write frame timeout %" PRIu64 "", rs->id, step1 - step0);
			}
			OSAL_TASK_Sleep(5000);
		}

		if (rs->stop_flag != 0) {
			OSAL_TASK_Sleep(20000);
			step1 = RECORDER_GetUs();
		}
	}
	CVI_LOGD("RS[%d] exit", rs->id);
}

 void master_sub_video_task_entry(void *arg) {
    rs_context_handle_t rs = (rs_context_handle_t)arg;
    REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

    uint64_t step0 = 0, step1 = RECORDER_GetUs();
    while (!rs->shutdown) {
        if (rs->stop_flag == 0) {
            step0 = RECORDER_GetUs();
            // if (step0 - step1 > 100 * 1000) {
            //     CVI_LOGW("task[%d] schedule timeout %"PRIu64"", rs->id, step0 - step1);
            // }

            if (p->enRecType == RECORDER_TYPE_NORMAL) {
                // process_one_subframe(rs);
            } else if (p->enRecType == RECORDER_TYPE_LAPSE) {
                process_one_subframe_timelapse(rs);
            }

            step1 = RECORDER_GetUs();
            if (step1 - step0 > 100 * 1000) {
                CVI_LOGW("task[%d] get && write frame timeout %"PRIu64"", rs->id, step1 - step0);
            }
            OSAL_TASK_Sleep(500 * 1000);
        }

        if (rs->stop_flag != 0) {
            OSAL_TASK_Sleep(20000);
            step1 = RECORDER_GetUs();
        }
    }
    CVI_LOGD("RS[%d] exit", rs->id);
}

static void disable_state_in_new_state(rs_context_handle_t rs, uint32_t disable_bits)
{
	rs->new_state &= (~disable_bits);
}

static int32_t start_mem_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	CVI_LOGD("RS[%d]: Start memory buffer", rs->id);

	if (rs->recorder[0] && (p->s32MemRecPreSec > 0)) {
		CHECK_RET(RECORDER_Start_MemRec(rs->recorder[0]));
	} else {
		return RS_ERR_FAILURE;
	}
	rs->stop_flag = 0;
	return RS_SUCCESS;
}

static int32_t stop_mem_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	CVI_LOGD("RS[%d]: Stop memory buffer", rs->id);

	if (rs->recorder[0] && (p->s32MemRecPreSec > 0)) {
		CHECK_RET(RECORDER_Stop_MemRec(rs->recorder[0]));
	} else {
		return RS_ERR_FAILURE;
	}

	return RS_SUCCESS;
}

static int32_t start_normal_rec(rs_context_handle_t rs)
{
	MAPI_VENC_CHN_ATTR_T venc_attr = {0};
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	p->enRecType = RECORDER_TYPE_NORMAL;

	CVI_LOGD("RS[%d]: Start Record %p", rs->id, p->handles.rec_venc_hdl);

	switch (p->recorder_file_type) {
	case RECORD_SERVICE_FILE_TYPE_MP4:
	case RECORD_SERVICE_FILE_TYPE_MOV:
	case RECORD_SERVICE_FILE_TYPE_TS: {
		if (RECORDER_Start_NormalRec(rs->recorder[0]) != 0) {
			return RS_ERR_FAILURE;
		}
	} break;

	case RECORD_SERVICE_FILE_TYPE_ES: {
		static int32_t i = 0;
		char outputfile[64];
		if (p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.enCodecType == MUXER_TRACK_VIDEO_CODEC_H264) {
			sprintf(outputfile, "%svid_%04d.h264", p->mntpath, i++);
		} else if (p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.enCodecType == MUXER_TRACK_VIDEO_CODEC_H265) {
			sprintf(outputfile, "%svid_%04d.h265", p->mntpath, i++);
		} else {
			CVI_LOG_ASSERT(0, "invalide video codec %d", p->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_VIDEO].unTrackSourceAttr.stVideoInfo.enCodecType);
		}

		rs->outfp = fopen(outputfile, "wb");
		if (rs->outfp == NULL) {
			CVI_LOGE("open file err, %s", outputfile);
			return RS_ERR_FAILURE;
		}
	} break;

	case RECORD_SERVICE_FILE_TYPE_NONE:
		break;

	default:
		CVI_LOGE("Unsupported recorder file type %d", p->recorder_file_type);
		return RS_ERR_INVALID;
	}

	pthread_mutex_lock(&rs->rec_mutex);
	reset_vproc_bind_state(rs, 1);
	if (p->handles.venc_rec_start == 0) {
		MAPI_VENC_GetAttr(p->handles.rec_venc_hdl, &venc_attr);
		if (!venc_attr.sbm_enable) {
			MAPI_VENC_StartRecvFrame(p->handles.rec_venc_hdl, -1);
		}
		MAPI_VPROC_EnableChn(p->handles.rec_vproc, p->handles.vproc_chn_id_venc);
#ifndef SERVICES_SUBVIDEO_ON
        if(p->enable_subvideo){
        	MAPI_VENC_StartRecvFrame(p->handles.sub_rec_venc_hdl, -1);
        }
#endif
		p->handles.venc_rec_start = 1;
	}
	rs->stop_flag = 0;

#ifdef SERVICES_SUBVIDEO_ON
    add_subvideo_callback_to_recorder(rs);
#endif
	pthread_mutex_unlock(&rs->rec_mutex);

	return RS_SUCCESS;
}

static int32_t stop_normal_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;

	switch (p->recorder_file_type) {
	case RECORD_SERVICE_FILE_TYPE_MP4:
	case RECORD_SERVICE_FILE_TYPE_MOV:
	case RECORD_SERVICE_FILE_TYPE_TS: {
		CVI_LOG_ASSERT(rs->recorder[0], "recorder not initialized");
		LOG_RET(RECORDER_Stop_NormalRec(rs->recorder[0]));
#ifdef SERVICES_SUBVIDEO_ON
		remove_subvideo_callback_to_recorder(rs);
#endif
		break;
	}

	case RECORD_SERVICE_FILE_TYPE_ES:
		CVI_LOG_ASSERT(rs->outfp, "outfp not initialized");
		fclose(rs->outfp);
		sync();
		break;

	case RECORD_SERVICE_FILE_TYPE_NONE:
		break;

	default:
		CVI_LOGE("Unsupported recorder file type %d", p->recorder_file_type);
		return RS_ERR_INVALID;
	}

	return RS_SUCCESS;
}

static int32_t start_event_rec(rs_context_handle_t rs)
{
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	if (p->handles.venc_rec_start == 0 || p->enRecType == RECORDER_TYPE_LAPSE) {
		return RS_SUCCESS;
	}
	if (0 != RECORDER_Start_EventRec(rs->recorder[0])) {
		CVI_LOGE("Event recorder start failed");
	}
	rs->stop_flag = 0;
	return RS_SUCCESS;
}

/*
static int32_t stop_event_rec(rs_context_handle_t rs) {
	CVI_LOGI("RS[%d]: Stop event recording", rs->id);

	if (rs->recorder[0]) {
		LOG_RET(RECORDER_Stop_EventRec(rs->recorder[0]));
	} else {
		CVI_LOGE("RS[%d]: Event recorder is NULL", rs->id);
	}

	return RS_SUCCESS;
}*/

static int32_t stop_event_rec_post(rs_context_handle_t rs)
{
	CVI_LOGD("RS[%d]: Stop event recorder post recording", rs->id);
	if (rs->recorder[0]) {
		LOG_RET(RECORDER_Stop_EventRecPost(rs->recorder[0]));
	} else {
		CVI_LOGE("RS[%d]: Event recorder is NULL", rs->id);
	}
	return RS_SUCCESS;
}

static int32_t start_timelapse_rec(rs_context_handle_t rs)
{
	MAPI_VENC_CHN_ATTR_T venc_attr = {0};
	REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
	p->enRecType = RECORDER_TYPE_LAPSE;

	RECORDER_Start_LapseRec(rs->recorder[0]);

	pthread_mutex_lock(&rs->rec_mutex);
	reset_vproc_bind_state(rs, 0);

	MAPI_VENC_GetAttr(p->handles.rec_venc_hdl, &venc_attr);

	if (p->handles.venc_rec_start == 0) {
		if (!venc_attr.sbm_enable) {
			MAPI_VENC_StartRecvFrame(p->handles.rec_venc_hdl, -1);
		}

		if(p->enable_subvideo){
            MAPI_VENC_StartRecvFrame(p->handles.sub_rec_venc_hdl, -1);
        }
		p->handles.venc_rec_start = 1;
	}
	rs->stop_flag = 0;
	pthread_mutex_unlock(&rs->rec_mutex);

	return RS_SUCCESS;
}

static int32_t stop_timelapse_rec(rs_context_handle_t rs)
{
	RECORDER_Stop_LapseRec(rs->recorder[0]);

	return RS_SUCCESS;
}

static int32_t handle_state_transition(rs_context_handle_t rs)
{

	CVI_LOGD("RS[%d]: RS_STATE 0x%x -> 0x%x", rs->id, rs->cur_state, rs->new_state);
	CVI_LOG_ASSERT(rs->cur_state != rs->new_state, "same state");

	if (RS_STATE_UP(rs, REC_CREATE)) {
		CHECK_RET(master_create_rec(rs));
	}

	if (RS_STATE_UP(rs, MEM_RECORD)) {
		int32_t ret = start_mem_rec(rs);
		if (ret != RS_SUCCESS) {
			CVI_LOGE("RS[%d]: start memory record failed [%d]", rs->id, ret);
			disable_state_in_new_state(rs, RS_STATE_MEM_RECORD_EN);
		}
	}

	if (RS_STATE_UP(rs, RECORD)) {
		int32_t ret = start_normal_rec(rs);
		if (ret != RS_SUCCESS) {
			CVI_LOGE("RS[%d]: start record failed [%d]", rs->id, ret);
			disable_state_in_new_state(rs, RS_STATE_RECORD_EN);
		}
	}

	if (RS_STATE_UP(rs, EVENT_RECORD)) {
		LOG_RET(start_event_rec(rs));
	}

	if (RS_STATE_UP(rs, TIMELAPSE_RECORD)) {
		LOG_RET(start_timelapse_rec(rs));
	}

	if (RS_STATE_UP(rs, MUTE)) {
		LOG_RET(start_mute(rs));
	}

	if (RS_STATE_UP(rs, PIV)) {
		CVI_LOG_ASSERT(0, "not support piv yet");
	}

	if (RS_STATE_UP(rs, PERF)) {
		LOG_RET(start_perf(rs));
	}

	if (RS_STATE_UP(rs, DEBUG)) {
		// nothing to do for start
	}

	if (RS_STATE_DOWN(rs, DEBUG)) {
		// nothing to do for stop
	}

	if (RS_STATE_DOWN(rs, PERF)) {
		LOG_RET(stop_perf(rs));
	}

	if (RS_STATE_DOWN(rs, PIV)) {
		CVI_LOG_ASSERT(0, "not support piv yet");
	}

	if (RS_STATE_DOWN(rs, MUTE)) {
		LOG_RET(stop_mute(rs));
	}

	if (RS_STATE_DOWN(rs, EVENT_RECORD)) {
		// LOG_RET(stop_event_rec(rs));
	}

	if (RS_STATE_UP(rs, STOP_EVENT_RECORD_POST_REC)) {
		LOG_RET(stop_event_rec_post(rs));
		disable_state_in_new_state(rs, RS_STATE_STOP_EVENT_RECORD_POST_REC_EN);
	}

	if (RS_STATE_DOWN(rs, RECORD)) {
		LOG_RET(stop_normal_rec(rs));
	}

	if (RS_STATE_DOWN(rs, MEM_RECORD)) {
		LOG_RET(stop_mem_rec(rs));
	}

	if (RS_STATE_DOWN(rs, TIMELAPSE_RECORD)) {
		LOG_RET(stop_timelapse_rec(rs));
	}

	if (RS_STATE_DOWN(rs, REC_CREATE)) {
		LOG_RET(master_destroy_rec(rs));
	}

	return RS_SUCCESS;
}

static void master_state_task_entry(void *arg)
{
	rs_context_handle_t rs = (rs_context_handle_t)arg;
	while (!rs->shutdown) {
		// handle transition
		pthread_mutex_lock(&rs->state_mutex);
		if (rs->new_state != rs->cur_state) {
			CHECK_RET(handle_state_transition(rs));
			rs->cur_state = rs->new_state;
			CVI_LOGD("RS[%d]: RS_STATE: change to 0x%x done", rs->id, rs->cur_state);
		}
		pthread_mutex_unlock(&rs->state_mutex);
		OSAL_TASK_Sleep(20 * 1000);
	}
	CVI_LOGD("RS[%d] exit", rs->id);
}

static int32_t master_start_task(rs_context_handle_t rs)
{

	static char state_name[16] = {0};
	snprintf(state_name, sizeof(state_name), "rs_state_%d", rs->id);
	OSAL_TASK_ATTR_S ta;
	ta.name = state_name;
	ta.entry = master_state_task_entry;
	ta.param = (void *)rs;
	ta.priority = OSAL_TASK_PRI_RT_HIGH;
	ta.detached = false;
	ta.stack_size = 256 * 1024;
	int32_t rc = OSAL_TASK_Create(&ta, &rs->state_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_state task create failed, %d", rc);
		return RS_ERR_FAILURE;
	}

	OSAL_TASK_ATTR_S va;
	static char rs_name[16] = {0};
	snprintf(rs_name, sizeof(rs_name), "rs_video_%d", rs->id);
	va.name = rs_name;
	va.entry = master_video_task_entry;
	va.param = (void *)rs;
	va.priority = OSAL_TASK_PRI_RT_MID + 20;
	va.detached = false;
	va.stack_size = 256 * 1024;
	rc = OSAL_TASK_Create(&va, &rs->video_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_video task create failed, %d", rc);
		return RS_ERR_FAILURE;
	}

    REC_ATTR_T *p = (REC_ATTR_T *)rs->attr;
    if(p->enable_subvideo){
        OSAL_TASK_ATTR_S sub_va;
        static char subv_name[16] = {0};
        snprintf(subv_name, sizeof(subv_name), "rs_sub_video_%d", rs->id);
        sub_va.name = subv_name;
        sub_va.entry = master_sub_video_task_entry;
        sub_va.param = (void *)rs;
        sub_va.priority = OSAL_TASK_PRI_RT_MID + 20;
        sub_va.detached = false;
        rc = OSAL_TASK_Create(&sub_va, &rs->sub_video_task);
        if (rc != OSAL_SUCCESS) {
            CVI_LOGE("rs_sub_video task create failed, %d", rc);
            return RS_ERR_FAILURE;
        }
    }

#ifdef ENABLE_SUBTITLE_ON
	OSAL_TASK_ATTR_S sa;
	static char sa_name[16] = {0};
	snprintf(sa_name, sizeof(sa_name), "rs_subtitle_%d", rs->id);
	sa.name = sa_name;
	sa.entry = master_subtitle_task_entry;
	sa.param = (void *)rs;
	sa.priority = OSAL_TASK_PRI_RT_MID;
	sa.detached = false;
	sa.stack_size = 256 * 1024;
	rc = OSAL_TASK_Create(&sa, &rs->subtitle_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_subtitle task create failed, %d", rc);
		return RS_ERR_FAILURE;
	}
#endif

#ifdef ENABLE_SNAP_ON
	OSAL_TASK_ATTR_S piv_ta;
	static char piv_name[16] = {0};
	snprintf(piv_name, sizeof(piv_name), "rs_piv_%d", rs->id);
	piv_ta.name = piv_name;
	piv_ta.entry = rs_snap_task_entry;
	piv_ta.param = (void *)rs;
	piv_ta.priority = OSAL_TASK_PRI_RT_LOW;
	piv_ta.detached = false;
	piv_ta.stack_size = 256 * 1024;
	rc = OSAL_TASK_Create(&piv_ta, &rs->piv_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_snap task create failed, %d", rc);
		return RS_ERR_FAILURE;
	}
#endif
	OSAL_TASK_ATTR_S thumb_ta;
	static char thumb_name[16] = {0};
	snprintf(thumb_name, sizeof(thumb_name), "rs_thumb_%d", rs->id);
	thumb_ta.name = thumb_name;
	thumb_ta.entry = rs_thumb_task_entry;
	thumb_ta.param = (void *)rs;
	thumb_ta.priority = OSAL_TASK_PRI_NORMAL;
	thumb_ta.detached = false;
	thumb_ta.stack_size = 256 * 1024;
	rc = OSAL_TASK_Create(&thumb_ta, &rs->thumb_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_thumb task create failed, %d", rc);
		return RS_ERR_FAILURE;
	}

	return RS_SUCCESS;
}

void *master_create(int32_t id, REC_ATTR_T *attr)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	memset(&gstRecMasterCtx[id], 0x0, sizeof(gstRecMasterCtx[id]));
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	handle->attr = attr;
	handle->id = id;
	if (g_rec_thumbnail_buf[id].size == 0 && attr->handles.thumbnail_bufsize != 0) {
		g_rec_thumbnail_buf[id].buf = malloc(attr->handles.thumbnail_bufsize);
		if (g_rec_thumbnail_buf[id].buf == NULL) {
			CVI_LOGE("g_rec_thumbnail_buf %d %u malloc failed, OOM!!!", id, attr->handles.thumbnail_bufsize);
			return NULL;
		}
		g_rec_thumbnail_buf[id].size = attr->handles.thumbnail_bufsize;
		CVI_LOGD("recorder thumbnail size %u", g_rec_thumbnail_buf[id].size);
	}
#ifdef ENABLE_SNAP_ON
	if (g_snap_thumbnail_buf[id].size == 0 && attr->handles.piv_bufsize != 0 && attr->handles.thumbnail_bufsize != 0) {
		g_snap_thumbnail_buf[id].buf = malloc(attr->handles.thumbnail_bufsize);
		if (g_snap_thumbnail_buf[id].buf == NULL) {
			CVI_LOGE("snap thumbnail %d %u malloc failed, OOM!!!", id, attr->handles.thumbnail_bufsize);
			return NULL;
		}
		g_snap_thumbnail_buf[id].size = attr->handles.thumbnail_bufsize;
		CVI_LOGD("snap thumbnail size %u", g_snap_thumbnail_buf[id].size);
	}

	if (g_snap_buf[id].size == 0 && attr->handles.piv_bufsize != 0) {
		g_snap_buf[id].buf = malloc(attr->handles.piv_bufsize);
		if (g_snap_buf[id].buf == NULL) {
			CVI_LOGE("snap buf %d %u malloc failed, OOM!!!", id, attr->handles.piv_bufsize);
			return NULL;
		}
		g_snap_buf[id].size = attr->handles.piv_bufsize;
		CVI_LOGD("snap buf size %u", g_snap_buf[id].size);
	}
#endif
	handle->stop_flag = 1;

	// OSAL_MUTEX_ATTR_S ma;
	// ma.name = "rs_state_lock";
	// int32_t rc = OSAL_MUTEX_Create(&ma, &handle->state_mutex);
	// if (rc != OSAL_SUCCESS) {
	//     CVI_LOGE("rs_state_lock create failed, %d\n", rc);
	//     return NULL;
	// }
	pthread_mutex_init(&handle->state_mutex, NULL);
	pthread_mutex_init(&handle->param_mutex, NULL);
#ifdef ENABLE_SNAP_ON
	pthread_mutex_init(&handle->piv_mutex, NULL);
	pthread_condattr_t piv_condattr;
	pthread_condattr_init(&piv_condattr);
	pthread_condattr_setclock(&piv_condattr, CLOCK_MONOTONIC);
	pthread_cond_init(&handle->piv_cond, &piv_condattr);
	pthread_condattr_destroy(&piv_condattr);
#endif
	pthread_mutex_init(&handle->thumbnail_mutex, NULL);
	pthread_mutex_init(&handle->rec_mutex, NULL);
	handle->cur_state = RS_STATE_IDLE;
	handle->piv_prealloclen = attr->s32SnapPresize;

	if (attr->enable_thumbnail && rs_thumbnail_chn_can_be_toggled(attr)) {
		if (MAPI_VPROC_DisableChn(attr->handles.thumbnail_vproc, attr->handles.vproc_chn_id_thumbnail) != 0) {
			CVI_LOGW("RS[%d]: initial disable thumbnail chn failed", id);
		}
	}

	master_start_task(handle);

	if (attr->enRecType == RECORDER_TYPE_NORMAL) {
		rs_enable_state(handle, RS_STATE_MUTE_EN);
		if (attr->astStreamAttr[0].aHTrackSrcHandle[RECORDER_TRACK_SOURCE_TYPE_AUDIO].enable == 1) {
			rs_disable_state(handle, RS_STATE_MUTE_EN);
		}
	}

	uint32_t enable_states = 0;

	enable_states |= RS_STATE_REC_CREATE_EN;

	if (attr->enable_record_on_start) {
		enable_states |= RS_STATE_RECORD_EN;
	}

	if (attr->enable_perf_on_start) {
		enable_states |= RS_STATE_PERF_EN;
	}
	if (attr->enable_debug_on_start) {
		enable_states |= RS_STATE_DEBUG_EN;
	}

	rs_enable_state(handle, enable_states);
	return (void *)handle;
}

int32_t master_destroy(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}

	rs_context_handle_t handle = &gstRecMasterCtx[id];
	REC_ATTR_T *p = (REC_ATTR_T *)handle->attr;

	CVI_LOGD("RS[%d] destroy start", id);
	rs_change_state(handle, RS_STATE_IDLE);
	handle->shutdown = 1;
	int32_t rc = OSAL_TASK_Join(handle->state_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_state task join failed, %d", rc);
		return RS_ERR_FAILURE;
	}
	OSAL_TASK_Destroy(&handle->state_task);

	rc = OSAL_TASK_Join(handle->video_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_video task join failed, %d", rc);
		return RS_ERR_FAILURE;
	}
	OSAL_TASK_Destroy(&handle->video_task);

	if(p->enable_subvideo){
        rc = OSAL_TASK_Join(handle->sub_video_task);
        if (rc != OSAL_SUCCESS) {
            CVI_LOGE("rs_sub_video task join failed, %d", rc);
            return RS_ERR_FAILURE;
        }
        OSAL_TASK_Destroy(&handle->sub_video_task);
    }

	rc = OSAL_TASK_Join(handle->subtitle_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("subtitle task join failed, %d", rc);
		return RS_ERR_FAILURE;
	}
	OSAL_TASK_Destroy(&handle->subtitle_task);

	rc = OSAL_TASK_Join(handle->thumb_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("thumbnail task join failed, %d", rc);
		return RS_ERR_FAILURE;
	}
	OSAL_TASK_Destroy(&handle->thumb_task);

	RECORDER_Destroy(&handle->recorder[0]);
	RECORDER_Destroy(&handle->recorder[1]);

#ifdef ENABLE_SNAP_ON
	pthread_mutex_lock(&handle->piv_mutex);
	pthread_cond_signal(&handle->piv_cond);
	pthread_mutex_unlock(&handle->piv_mutex);
	rc = OSAL_TASK_Join(handle->piv_task);
	if (rc != OSAL_SUCCESS) {
		CVI_LOGE("rs_piv task join failed, %d", rc);
		return RS_ERR_FAILURE;
	}
	OSAL_TASK_Destroy(&handle->piv_task);
	CVI_LOGD("RS[%d] destroy start 1", id);
	pthread_mutex_destroy(&handle->piv_mutex);
	pthread_cond_destroy(&handle->piv_cond);
#endif

	pthread_mutex_destroy(&handle->state_mutex);
	pthread_mutex_destroy(&handle->param_mutex);
	pthread_mutex_destroy(&handle->thumbnail_mutex);
	pthread_mutex_destroy(&handle->rec_mutex);

	if (g_rec_thumbnail_buf[id].buf) {
		free(g_rec_thumbnail_buf[id].buf);
		g_rec_thumbnail_buf[id].buf = NULL;
		CVI_LOGD("## free g_rec_thumbnail_buf %d %u", id, g_rec_thumbnail_buf[id].size);
	}
	g_rec_thumbnail_buf[id].actsize = 0;
	g_rec_thumbnail_buf[id].size = 0;
#ifdef ENABLE_SNAP_ON
	if (g_snap_thumbnail_buf[id].buf) {
		free(g_snap_thumbnail_buf[id].buf);
		g_snap_thumbnail_buf[id].buf = NULL;
		CVI_LOGD("## free g_snap_thumbnail_buf %d %u", id, g_snap_thumbnail_buf[id].size);
	}
	g_snap_thumbnail_buf[id].actsize = 0;
	g_snap_thumbnail_buf[id].size = 0;

	if (g_snap_buf[id].buf) {
		free(g_snap_buf[id].buf);
		g_snap_buf[id].buf = NULL;
		CVI_LOGD("## free g_snap_buf %d %u", id, g_snap_buf[id].size);
	}
	g_snap_buf[id].actsize = 0;
	g_snap_buf[id].size = 0;
#endif
	CVI_LOGD("RS[%d]end", id);

	return rc;
}

int32_t master_update_attr(int32_t id, REC_ATTR_T *attr)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}

	rs_context_handle_t handle = &gstRecMasterCtx[id];
	handle->attr = attr;
	rs_disable_state(handle, RS_STATE_REC_CREATE_EN);
	rs_enable_state(handle, RS_STATE_REC_CREATE_EN);
	return 0;
}

int32_t master_start_normal_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_enable_state(handle, RS_STATE_RECORD_EN);
	return 0;
}

int32_t master_stop_normal_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_disable_state(handle, RS_STATE_RECORD_EN);
	return 0;
}

int32_t master_start_lapse_rec(int32_t id)
{
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_enable_state(handle, RS_STATE_TIMELAPSE_RECORD_EN);
	return 0;
}

int32_t master_stop_lapse_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_disable_state(handle, RS_STATE_TIMELAPSE_RECORD_EN);
	return 0;
}

int32_t master_start_event_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_enable_state(handle, RS_STATE_EVENT_RECORD_EN);
	rs_disable_state(handle, RS_STATE_EVENT_RECORD_EN);
	return 0;
}

int32_t master_stop_event_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_enable_state(handle, RS_STATE_STOP_EVENT_RECORD_POST_REC_EN);
	return 0;
}

int32_t master_start_mem_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	if (((REC_ATTR_T *)handle->attr)->s32MemRecPreSec <= 0) {
		return 0;
	}

	rs_enable_state(handle, RS_STATE_MEM_RECORD_EN);
	return 0;
}

int32_t master_stop_mem_rec(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	if (((REC_ATTR_T *)handle->attr)->s32MemRecPreSec <= 0) {
		return 0;
	}

	rs_disable_state(handle, RS_STATE_MEM_RECORD_EN);
	return 0;
}

int32_t master_set_mute(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_enable_state(handle, RS_STATE_MUTE_EN);
	return 0;
}

int32_t master_cancle_mute(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	rs_disable_state(handle, RS_STATE_MUTE_EN);
	return 0;
}

int32_t master_snap(int32_t id, char *file_name)
{
	int32_t ret = 0;
	if (id >= MAX_CONTEXT_CNT) {
		return 0;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];

	while (0 == handle->piv_shared) {
		OSAL_TASK_Sleep(10 * 1000);
	}

	pthread_mutex_lock(&handle->piv_mutex);
	handle->piv_shared = 0;
	memset(handle->piv_filename, 0, sizeof(handle->piv_filename));
	snprintf(handle->piv_filename, sizeof(handle->piv_filename), "%s", file_name);

	if (0 == strlen(handle->piv_filename)) {
		CVI_LOGE("get snap filename failed! \n");
		ret = -1;
	}
	handle->piv_finish = 0;
	pthread_mutex_unlock(&handle->piv_mutex);
	pthread_cond_signal(&handle->piv_cond);
	CVI_LOGD("%d snap_finish", id);
	return ret;
}

void master_waitsnap_finish(int32_t id)
{
	if (id >= MAX_CONTEXT_CNT) {
		CVI_LOGE("record ID is error\n");
		return;
	}
	rs_context_handle_t handle = &gstRecMasterCtx[id];
	CVI_LOGD("%d waitsnap_start", id);
	while (!handle->piv_finish) {
		OSAL_TASK_Sleep(20 * 1000);
	}
	CVI_LOGD("%d waitsnap_finish", id);
}
