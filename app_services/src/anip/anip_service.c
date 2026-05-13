/**
 * @file anip_service.c
 * @brief 动物识别服务实现
 *
 * 参考 facep_service.c 的架构，从 VPSS 获取帧并传递给 kt_ani_sdk
 */
#include "anip_service.h"
#include "cvi_vpss.h"
#include "cvi_sys.h"
#include "cvi_comm_video.h"
#include <unistd.h>
#include <math.h>
#include "animal_labels.h"
#include <inttypes.h>
/* 调试日志宏 */
#define ANIP_LOGI(fmt, ...) CVI_LOGI("[ANIP] " fmt, ##__VA_ARGS__)
#define ANIP_LOGE(fmt, ...) CVI_LOGE("[ANIP] " fmt, ##__VA_ARGS__)

#define ANIP_DEBUG_SAVE_FRAME 1

/* 最大服务实例数 */
#define MAX_ANIP_CNT MAX_CAMERA_INSTANCES

/* 动物识别上下文结构体 */
typedef struct _ANIP_CONTEXT_T {
    CVI_S32 handle;
    pthread_mutex_t mutex;
    CVI_S32 is_running;
    OSAL_TASK_HANDLE_S task_handle;
    ANIP_SERVICE_PARAM_S param;
    CVI_U8* frame_buf;          /* 预分配帧数据缓冲区，避免重复malloc */
    CVI_U32 frame_buf_size;     /* 当前缓冲区大小 */
} ANIP_CONTEXT_T;

/* 全局上下文数组 */
static ANIP_CONTEXT_T g_anip_context[MAX_ANIP_CNT];
static CVI_S32 g_anip_is_pause = 0;
static CVI_S32 g_anip_is_pause_ok = 1;
static ANIP_SERVICE_HANDLE_T g_anip_handle_map[MAX_ANIP_CNT] = {0};

/* 回调函数指针 */
static AnipDrawRectFun g_draw_rect_func = NULL;
static AnipResultFun g_result_func = NULL;

/* 全局初始化标志 */
static CVI_S32 g_anip_init_done = 0;

/* 检测框数组 */
static RECT_S g_anip_rects[MAX_ANIMAL_CNT] = {0};

/* 检查是否为动物类别 */
/* cls_idx: 0=animal, 1=person, 2=vehicle */
static inline CVI_BOOL is_animal_class(int cls_idx) {
    /* 临时：绘制所有检测到的目标（人、车、动物）以便调试 */
    return (cls_idx >= 0 && cls_idx <= 2);
    /* 正式版本：只绘制动物 */
    // return (cls_idx == 0);
}

/* 动物识别结果数组（用于回调），名字由服务内部转换 */
static ANIP_RESULT_S g_anip_results[MAX_ANIMAL_CNT] = {0};



CVI_S32 SAMPLE_VPSS_FrameSaveToFile(const CVI_CHAR *filename, VIDEO_FRAME_INFO_S *pstVideoFrame)
{
	CVI_S32 s32Ret = CVI_SUCCESS;
	FILE *fp;
	CVI_U32 u32len, u32DataLen;

	fp = fopen(filename, "w");
	if (fp == CVI_NULL) {
		ANIP_LOGI("open data file(%s) error\n", filename);
		return CVI_FAILURE;
	}

	for (int i = 0; i < 3; ++i) {
		u32DataLen = pstVideoFrame->stVFrame.u32Stride[i] * pstVideoFrame->stVFrame.u32Height;
		if (u32DataLen == 0)
			continue;
		if (i > 0 && ((pstVideoFrame->stVFrame.enPixelFormat == PIXEL_FORMAT_YUV_PLANAR_420) ||
			(pstVideoFrame->stVFrame.enPixelFormat == PIXEL_FORMAT_NV12) ||
			(pstVideoFrame->stVFrame.enPixelFormat == PIXEL_FORMAT_NV21)))
			u32DataLen >>= 1;

		pstVideoFrame->stVFrame.pu8VirAddr[i]
			= CVI_SYS_Mmap(pstVideoFrame->stVFrame.u64PhyAddr[i], pstVideoFrame->stVFrame.u32Length[i]);

        ANIP_LOGI("plane(%d): paddr(%#"PRIx64") vaddr(%p) stride(%d)\n",
			   i, pstVideoFrame->stVFrame.u64PhyAddr[i],
			   pstVideoFrame->stVFrame.pu8VirAddr[i],
			   pstVideoFrame->stVFrame.u32Stride[i]);
        ANIP_LOGI(" data_len(%d) plane_len(%d)\n",
			      u32DataLen, pstVideoFrame->stVFrame.u32Length[i]);
		u32len = fwrite(pstVideoFrame->stVFrame.pu8VirAddr[i], u32DataLen, 1, fp);
		if (u32len <= 0) {
			ANIP_LOGI("fwrite data(%d) error\n", i);
			s32Ret = CVI_FAILURE;
			break;
		}
		CVI_SYS_Munmap(pstVideoFrame->stVFrame.pu8VirAddr[i], pstVideoFrame->stVFrame.u32Length[i]);
	}

	fclose(fp);
	return s32Ret;
}

/* 绘制检测框 - 完全参考 facep_draw_rect */
static CVI_S32 anip_draw_rect(CVI_U32 osd_id, CVI_U32 osd_w, CVI_U32 osd_h,
                              CVI_U32 osd_mirror, CVI_U32 osd_flip,
                              struct KTAniInfo* ani_results, CVI_U32 result_count) {
    CVI_U32 i = 0;
    CVI_U32 draw_count = 0;
    CVI_S32 ret = CVI_SUCCESS;
    (void)osd_flip;

    memset(g_anip_rects, 0, sizeof(g_anip_rects));

    for (i = 0; i < result_count && i < MAX_ANIMAL_CNT; i++) {
        /* 检查 bbox.valid 而不是外层 valid */
        if (!ani_results[i].bbox.valid) {
            continue;
        }

        if (!is_animal_class(ani_results[i].bbox.cls_idx)) {
            continue;
        }

        /* KTBoundingBox: x,y 是中心点，已经是像素坐标 */
        float x1 = ani_results[i].bbox.x - ani_results[i].bbox.width / 2.0f;
        float y1 = ani_results[i].bbox.y - ani_results[i].bbox.height / 2.0f;
        float x2 = ani_results[i].bbox.x + ani_results[i].bbox.width / 2.0f;
        float y2 = ani_results[i].bbox.y + ani_results[i].bbox.height / 2.0f;

        /* 已经是像素坐标，不需要乘 osd_w/osd_h */
        CVI_S32 px1 = (CVI_S32)x1;
        CVI_S32 py1 = (CVI_S32)y1;
        CVI_S32 px2 = (CVI_S32)x2;
        CVI_S32 py2 = (CVI_S32)y2;

        /* 边界检查 */
        if (px1 < 0) px1 = 0;
        if (py1 < 0) py1 = 0;
        if (px2 > (CVI_S32)osd_w) px2 = osd_w;
        if (py2 > (CVI_S32)osd_h) py2 = osd_h;

        /* 镜像处理 */
        if (osd_mirror) {
            g_anip_rects[draw_count].s32X = osd_w - px2;
            g_anip_rects[draw_count].s32Y = py1;
            g_anip_rects[draw_count].u32Width = px2 - px1;
            g_anip_rects[draw_count].u32Height = py2 - py1;
        } else {
            g_anip_rects[draw_count].s32X = px1;
            g_anip_rects[draw_count].s32Y = py1;
            g_anip_rects[draw_count].u32Width = px2 - px1;
            g_anip_rects[draw_count].u32Height = py2 - py1;
        }

        draw_count++;
    }

    /* 调用回调绘制矩形框 */
    if (g_draw_rect_func != NULL && draw_count > 0) {
        ret = g_draw_rect_func(osd_id, draw_count, g_anip_rects);
    }

    return ret;
}

/* 清除检测框 - 直接调用回调清除OSD上的矩形框 */
static CVI_S32 anip_clear_rect(CVI_U32 osd_id) {
    /* 直接调用回调，传入 num=0 通知OSD清除所有框 */
    if (g_draw_rect_func != NULL) {
        return g_draw_rect_func(osd_id, 0, NULL);
    }
    return CVI_SUCCESS;
}

/* 获取动物名称 - 使用 animal_labels.h 中的函数 */
static void anip_get_animal_name(CVI_U32 cls_idx, CVI_CHAR* name, CVI_U32 name_len) {
    const char* label = get_animal_name_by_id(cls_idx);
    if (label != NULL) {
        snprintf(name, name_len, "%s", label);
    } else {
        snprintf(name, name_len, "Unknown");
    }
}

/* 动物识别任务入口函数 - 参考 facep_task_entry 的流程 */
static CVI_VOID anip_task_entry(CVI_VOID *arg) {
    ANIP_CONTEXT_T* pcontext = (ANIP_CONTEXT_T*)arg;
    ANIP_SERVICE_PARAM_S* pparam = &pcontext->param;
    VPSS_GRP in_vpss_grp = pparam->in_vpss_grp;
    VPSS_CHN in_vpss_chn = pparam->in_vpss_chn;
    VIDEO_FRAME_INFO_S in_frame = {0};
    KTFrameInfo frame_info;
    CVI_S32 ret = CVI_SUCCESS;
    CVI_U32 detect_enable = 0;
    CVI_U32 rec_enable = 0;
    CVI_U32 if_disable_need_remove_rect = 0;
    struct KTAniInfo ani_results[MAX_ANIMAL_CNT] = {0};
    int result_count = MAX_ANIMAL_CNT;
    CVI_U32 i = 0;
    // CVI_U32 wait_frame_count = 0;
    // CVI_U32 max_wait_frames = 100;  /* 最多等待100次获取帧 */
    // CVI_BOOL vpss_chn_ready = CVI_FALSE;

    ANIP_LOGI("anip task is running, vpss_grp=%d, vpss_chn=%d", in_vpss_grp, in_vpss_chn);

    /* 检查 VPSS 通道属性 */
    VPSS_CHN_ATTR_S stChnAttr = {0};
    ret = CVI_VPSS_GetChnAttr(in_vpss_grp, in_vpss_chn, &stChnAttr);
    if (ret != CVI_SUCCESS) {
        ANIP_LOGE("Get VPSS Chn Attr failed: %#x, will try to enable", ret);
        /* 尝试使能通道 */
        ret = CVI_VPSS_EnableChn(in_vpss_grp, in_vpss_chn);
        if (ret != CVI_SUCCESS) {
            ANIP_LOGE("Enable VPSS Chn failed: %#x", ret);
        }
    }

    while (pcontext->is_running) {
        if (g_anip_is_pause) {
            g_anip_is_pause_ok = 1;
            sleep(1);
            continue;
        } else {
            g_anip_is_pause_ok = 0;
        }

        detect_enable = pparam->det_enable;
        rec_enable = pparam->rec_enable;

        /* 获取视频帧 - 使用较短超时，避免长时间阻塞 */
        ret = CVI_VPSS_GetChnFrame(in_vpss_grp, in_vpss_chn, &in_frame, 1000);

        if (ret != CVI_SUCCESS) {
            /* 错误码 0xc006800e 表示通道未使能或无帧，忽略 */
            if (ret != (CVI_S32)0xc006800e) {
                static CVI_U32 error_count = 0;
                error_count++;
                if (error_count % 50 == 1) {
                    ANIP_LOGE("Grp(%d)-Chn(%d) get frame failed with %#x (count=%d)", 
                              in_vpss_grp, in_vpss_chn, ret, error_count);
                }
            }
            usleep(100*1000);
            continue;
        }

        /* 检查帧有效性 */
        if (in_frame.stVFrame.u32Width == 0 || in_frame.stVFrame.u32Height == 0) {
            ANIP_LOGE("Frame size invalid: %dx%d", in_frame.stVFrame.u32Width, in_frame.stVFrame.u32Height);
            CVI_VPSS_ReleaseChnFrame(in_vpss_grp, in_vpss_chn, &in_frame);
            usleep(100*1000);
            continue;
        }

        /* 清零结果 */
        memset(ani_results, 0, sizeof(ani_results));
        result_count = MAX_ANIMAL_CNT;
        /* 执行动物检测 */
        if (detect_enable) {
            memset(&frame_info, 0, sizeof(KTFrameInfo));

            /* 获取帧信息 */
            PIXEL_FORMAT_E pix_fmt = (PIXEL_FORMAT_E)in_frame.stVFrame.enPixelFormat;
            CVI_U32 frame_width = in_frame.stVFrame.u32Width;
            CVI_U32 frame_height = in_frame.stVFrame.u32Height;
            // CVI_U32 y_stride = in_frame.stVFrame.u32Stride[0];
            // CVI_U64 y_phy_addr = in_frame.stVFrame.u64PhyAddr[0];
            // CVI_U64 uv_phy_addr = in_frame.stVFrame.u64PhyAddr[1];
            CVI_U8* pFrameData = in_frame.stVFrame.pu8VirAddr[0];

            /* 计算各平面大小 - 与 SAMPLE_VPSS_FrameSaveToFile 一致 */
            CVI_U32 plane_sizes[3] = {0};
            CVI_U32 plane_copy_sizes[3] = {0};
            CVI_U64 plane_phys[3] = {0};
            CVI_U32 plane_lengths[3] = {0};

            for (int i = 0; i < 3; i++) {
                plane_phys[i] = in_frame.stVFrame.u64PhyAddr[i];
                plane_lengths[i] = in_frame.stVFrame.u32Length[i];
                plane_sizes[i] = in_frame.stVFrame.u32Stride[i] * in_frame.stVFrame.u32Height;
                if (i > 0 && IS_FMT_YUV420(pix_fmt)) {
                    plane_copy_sizes[i] = plane_sizes[i] >> 1;  /* UV 平面减半 */
                } else {
                    plane_copy_sizes[i] = plane_sizes[i];
                }
            }

            /* 计算总缓冲区大小 */
            CVI_U32 total_copy_size = plane_copy_sizes[0] + plane_copy_sizes[1] + plane_copy_sizes[2];

            // ANIP_LOGI("Frame: %dx%d, fmt=%d, virAddr=0x%p, phyY=0x%llx, stride=%d",
            //           frame_width, frame_height, pix_fmt, pFrameData, y_phy_addr, y_stride);
            // ANIP_LOGI("Plane0: paddr=%llx, len=%d, stride=%d", plane_phys[0], plane_lengths[0], in_frame.stVFrame.u32Stride[0]);
            // ANIP_LOGI("Plane1: paddr=%llx, len=%d, stride=%d", plane_phys[1], plane_lengths[1], in_frame.stVFrame.u32Stride[1]);
            // ANIP_LOGI("Plane2: paddr=%llx, len=%d, stride=%d", plane_phys[2], plane_lengths[2], in_frame.stVFrame.u32Stride[2]);

            /* 如果虚拟地址为空，需要手动映射 - 与 SAMPLE_VPSS_FrameSaveToFile 一致 */
            if (pFrameData == NULL) {
                /* 使用预分配的缓冲区，避免每次循环 malloc/free 导致内存碎片 */
                if (total_copy_size > pcontext->frame_buf_size) {
                    /* 缓冲区不够大，重新分配 */
                    if (pcontext->frame_buf) {
                        ANIP_LOGI("Realloc frame buffer: %u -> %u", pcontext->frame_buf_size, total_copy_size);
                        free(pcontext->frame_buf);
                    }
                    pcontext->frame_buf = (CVI_U8*)malloc(total_copy_size);
                    if (pcontext->frame_buf == NULL) {
                        ANIP_LOGE("Failed to allocate frame buffer, size=%d", total_copy_size);
                        pcontext->frame_buf_size = 0;
                        CVI_VPSS_ReleaseChnFrame(in_vpss_grp, in_vpss_chn, &in_frame);
                        usleep(100*1000);
                        continue;
                    }
                    pcontext->frame_buf_size = total_copy_size;
                }
                pFrameData = pcontext->frame_buf;

                /* 第一阶段：先完成所有平面的映射，保存虚拟地址 */
                CVI_BOOL mmap_all_ok = CVI_TRUE;
                CVI_U8* mapped_ptrs[3] = {NULL, NULL, NULL};
                
                for (int i = 0; i < 3; i++) {
                    if (plane_phys[i] == 0 || plane_lengths[i] == 0) {
                        continue;
                    }

                    /* 使用 u32Length[i] 作为 mmap 大小 - 与 SAMPLE_VPSS_FrameSaveToFile 一致 */
                    mapped_ptrs[i] = (CVI_U8*)CVI_SYS_Mmap(plane_phys[i], plane_lengths[i]);
                    if (mapped_ptrs[i] == NULL) {
                        ANIP_LOGE("Plane %d: mmap failed, paddr=%llx, len=%d", i, plane_phys[i], plane_lengths[i]);
                        mmap_all_ok = CVI_FALSE;
                        /* 清理已映射的内存 */
                        for (int j = 0; j < i; j++) {
                            if (mapped_ptrs[j]) {
                                CVI_SYS_Munmap(mapped_ptrs[j], plane_lengths[j]);
                                mapped_ptrs[j] = NULL;
                            }
                        }
                        break;
                    }
                    CVI_SYS_IonInvalidateCache(plane_phys[i], mapped_ptrs[i], plane_lengths[i]);
                }

                /* 如果所有映射都成功，再进行复制 */
                if (!mmap_all_ok) {
                    /* 缓冲区保留，下次可再用 */
                    CVI_VPSS_ReleaseChnFrame(in_vpss_grp, in_vpss_chn, &in_frame);
                    usleep(100*1000);
                    continue;
                }

                /* 第二阶段：所有平面映射完成后，再进行数据复制 - 与 SAMPLE_VPSS_FrameSaveToFile 对齐 */
                CVI_U8* pDst = pFrameData;
                for (int i = 0; i < 3; i++) {
                    if (mapped_ptrs[i] == NULL) {
                        continue;
                    }
                    memcpy(pDst, mapped_ptrs[i], plane_copy_sizes[i]);
                    pDst += plane_copy_sizes[i];
                }

                /* 第三阶段：所有复制完成后，再统一取消所有映射 */
                for (int i = 0; i < 3; i++) {
                    if (mapped_ptrs[i]) {
                        CVI_SYS_Munmap(mapped_ptrs[i], plane_lengths[i]);
                        mapped_ptrs[i] = NULL;
                    }
                }
            }

            /* 构建 KTFrameInfo - 与 kt_ani_sdk 要求一致 */
            if (IS_FMT_YUV420(pix_fmt)) {
                frame_info.data = pFrameData;
                frame_info.format = KT_PIXEL_FORMAT_NV12;
            } else {
                frame_info.data = pFrameData;
                frame_info.format = KT_PIXEL_FORMAT_RGB;
            }
            frame_info.width = frame_width;
            frame_info.height = frame_height;

#ifdef ANIP_DEBUG_SAVE_FRAME
            {
                static int save_count = 0;
                char filename[64];
                snprintf(filename, sizeof(filename), "/mnt/sd/anip_frame_%03d_%dx%d.nv12",
                         save_count++, frame_info.width, frame_info.height);
                FILE *fp = fopen(filename, "wb");
                if (fp)
                {
                    /* 写入 Y 平面 + UV 平面 (NV12格式) */
                    fwrite(pFrameData, 1, total_copy_size, fp);
                    fclose(fp);
                }
            }
#endif
            /* 调试：保存 frame_info.data 到文件检查 YUV 数据是否正常 */
            ret = kt_ani_task_from_memory(&frame_info, KT_TASK_DET_ANI, ani_results, &result_count);
            if (ret != KT_ANI_OK)
            {
#if 0
                {
                    static int save_count = 0;
                    char filename[64];
                    snprintf(filename, sizeof(filename), "/mnt/sd/anip_frame_%03d_%dx%d.nv12",
                             save_count++, frame_info.width, frame_info.height);
                    FILE *fp = fopen(filename, "wb");
                    if (fp)
                    {
                        /* 写入 Y 平面 + UV 平面 (NV12格式) */
                        fwrite(pFrameData, 1, total_copy_size, fp);
                        fclose(fp);
                    }
                }
#endif
                ANIP_LOGE("kt_ani_task_from_memory failed, ret=%d result_count:%d", ret, result_count);
            }

            /* 使用预分配缓冲区，不需要在此处 free（anip_stop 时统一释放） */
        }

        /* 释放视频帧 - 与 facep_service 顺序一致 */
        ret = CVI_VPSS_ReleaseChnFrame(in_vpss_grp, in_vpss_chn, &in_frame);
        if (ret != CVI_SUCCESS) {
            ANIP_LOGE("Grp(%d)-Chn(%d) release frame failed with %#x", in_vpss_grp, in_vpss_chn, ret);
        }

        /* 绘制检测框 - 与 facep_service 一致 */
        if (detect_enable) {
            if_disable_need_remove_rect = 1;
            anip_draw_rect(pparam->osd_id, pparam->in_width, pparam->in_height,
                          pparam->osd_mirror, 0, ani_results, result_count);
        } else {
            if (if_disable_need_remove_rect) {
                anip_clear_rect(pparam->osd_id);
                if_disable_need_remove_rect = 0;
            }
        }

        /* 执行动物识别获取类别名称 */
        if (rec_enable && result_count > 0) {
            memset(g_anip_results, 0, sizeof(g_anip_results));
            CVI_U32 name_count = 0;
            for (i = 0; i < (CVI_U32)result_count && i < MAX_ANIMAL_CNT; i++) {
                /* 检查 bbox.valid 而不是外层 valid */
                if (ani_results[i].bbox.valid && is_animal_class(ani_results[i].bbox.cls_idx)) {
                    /* 使用 ani_idx (动物类别索引) 而不是 cls_idx */
                    anip_get_animal_name(ani_results[i].ani_idx,
                                        g_anip_results[name_count].name, sizeof(g_anip_results[name_count].name));
                    /* 保存类别索引 */
                    g_anip_results[name_count].cls_idx = ani_results[i].bbox.cls_idx;
                    // ANIP_LOGI("animal[%u]: cls_idx=%d, ani_idx=%d, name=%s", 
                    //     name_count, ani_results[i].bbox.cls_idx, ani_results[i].ani_idx, g_anip_results[name_count].name);
                    name_count++;
                }
            }
            if (g_result_func != NULL && name_count > 0) {
                g_result_func(pparam->osd_id, g_anip_results, name_count);
            }
        }

        usleep(100*1000);
    }

    g_anip_is_pause_ok = 1;
    ANIP_LOGI("anip task is exit");
}

/* 初始化动物识别引擎 */
static CVI_S32 anip_proc_init(ANIP_CONTEXT_T* pcontext) {
    (void)pcontext;  /* pcontext 暂未使用 */
    CVI_LOGI("ANIP init ------------------> start");

    if (g_anip_init_done) {
        CVI_LOGI("ANIP engine already initialized");
        return CVI_SUCCESS;
    }

    // 初始化动物识别引擎

    /* 初始化 kt_ani_sdk */
    KTAniError err = kt_ani_init("/mnt/data/bin/ai_model/", "/mnt/data/bin/ai_model/", "/mnt/data/bin/ai_model/");
    if (err != KT_ANI_OK) {
        ANIP_LOGE("kt_ani_init failed, err=%d", err);
        if (err == -102) {
            ANIP_LOGI("License online auth failed, retry after 3 seconds...");
            sleep(3);
            err =  kt_ani_init("/mnt/data/bin/ai_model/","/mnt/data/bin/ai_model/", "/mnt/data/bin/ai_model/");
            if (err != KT_ANI_OK) {
                ANIP_LOGE("kt_ani_init retry failed, err=%d", err);
                return ANIP_FAILURE;
            }
        } else {
            return ANIP_FAILURE;
        }
    }

    /* 设置检测灵敏度 */
    float sensitivity = 30.0f / 100.0f;
    err = set_detection_Sensitivity(sensitivity);
    (void)err;  /* 忽略返回值 */

    g_anip_init_done = 1;
    CVI_LOGI("ANIP init ------------------> done");
    return CVI_SUCCESS;
}

/* 启动动物识别任务 */
static CVI_S32 anip_start(ANIP_CONTEXT_T* pcontext) {
    CVI_S32 ret = CVI_SUCCESS;

    ANIP_LOGI("anip_start in");

    if (pcontext->is_running) {
        ANIP_LOGE("ANIP has started");
        return CVI_SUCCESS;
    }

    ret = anip_proc_init(pcontext);
    if (ret != CVI_SUCCESS) {
        ANIP_LOGE("anip_proc_init failed with %#x", ret);
        return ANIP_FAILURE;
    }

    pcontext->is_running = 1;
    g_anip_is_pause = 0;
    pthread_mutex_init(&pcontext->mutex, NULL);

    OSAL_TASK_ATTR_S task_attr;
    static char task_name[16] = {0};
    snprintf(task_name, sizeof(task_name), "anip_%d", pcontext->handle);
    task_attr.name = task_name;
    task_attr.entry = anip_task_entry;
    task_attr.param = (void*)pcontext;
    task_attr.priority = OSAL_TASK_PRI_RT_MID;
    task_attr.detached = CVI_FALSE;
    task_attr.stack_size = 256 * 1024;

    ret = OSAL_TASK_Create(&task_attr, &pcontext->task_handle);
    if (ret != OSAL_SUCCESS) {
        ANIP_LOGE("anip task create failed, %#x", ret);
        return ANIP_FAILURE;
    }

    return CVI_SUCCESS;
}

/* 停止动物识别任务 */
static CVI_S32 anip_stop(ANIP_CONTEXT_T* pcontext) {
    CVI_S32 ret = CVI_SUCCESS;
    ANIP_SERVICE_PARAM_S* pparam = NULL;

    ANIP_LOGI("anip_stop in");

    if (!pcontext->is_running) {
        ANIP_LOGE("ANIP has stopped");
        return CVI_SUCCESS;
    }

    pparam = &pcontext->param;
    pcontext->is_running = 0;

    ret = OSAL_TASK_Join(pcontext->task_handle);
    if (ret != OSAL_SUCCESS) {
        ANIP_LOGE("anip task join failed, %#x", ret);
        return ANIP_FAILURE;
    }
    OSAL_TASK_Destroy(&pcontext->task_handle);

    /* 清除检测框 */
    anip_clear_rect(pparam->osd_id);

    /* 释放预分配的帧缓冲区 */
    if (pcontext->frame_buf) {
        free(pcontext->frame_buf);
        pcontext->frame_buf = NULL;
        pcontext->frame_buf_size = 0;
    }

    pthread_mutex_destroy(&pcontext->mutex);
    return ret;
}

/* 获取句柄 */
static CVI_S32 anip_get_handle(ANIP_SERVICE_HANDLE_T* phandle) {
    CVI_U32 i = 0;
    ANIP_SERVICE_HANDLE_T handle = -1;

    for (i = 0; i < MAX_ANIP_CNT; i++) {
        if (g_anip_handle_map[i] == 0) {
            g_anip_handle_map[i] = 1;
            handle = i;
            break;
        }
    }

    if (handle == -1) {
        ANIP_LOGE("no handle");
        return ANIP_FAILURE;
    } else {
        *phandle = handle;
    }
    return CVI_SUCCESS;
}

/* 释放句柄 */
static CVI_S32 anip_reset_handle(ANIP_SERVICE_HANDLE_T handle) {
    g_anip_handle_map[handle] = 0;
    return CVI_SUCCESS;
}

/* API 实现 */
CVI_S32 ANIP_SERVICE_Create(ANIP_SERVICE_HANDLE_T* phandle, ANIP_SERVICE_PARAM_S* pparam) {
    CVI_S32 ret = CVI_SUCCESS;
    ANIP_CONTEXT_T* pcontext = NULL;

    ret = anip_get_handle(phandle);
    if (ret != CVI_SUCCESS) {
        ANIP_LOGE("get handle failed");
        return ANIP_FAILURE;
    } else {
        pcontext = &g_anip_context[*phandle];
        pcontext->handle = *phandle;
        memcpy(&pcontext->param, pparam, sizeof(ANIP_SERVICE_PARAM_S));
    }

    if (!pparam->det_enable && !pparam->rec_enable) {
        ANIP_LOGE("both det(%d) and rec(%d) disabled", pparam->det_enable, pparam->rec_enable);
        g_anip_is_pause_ok = 1;
        return CVI_SUCCESS;
    } else {
        ret = anip_start(pcontext);
    }

    return ret;
}

CVI_S32 ANIP_SERVICE_Destroy(ANIP_SERVICE_HANDLE_T handle) {
    CVI_S32 ret = CVI_SUCCESS;
    ANIP_CONTEXT_T* pcontext = NULL;

    if (handle < MAX_ANIP_CNT && handle >= 0) {
        pcontext = &g_anip_context[handle];
        ret = anip_stop(pcontext);
        anip_reset_handle(handle);
    } else {
        ANIP_LOGE("invalid handle:%d", handle);
        return ANIP_FAILURE;
    }

    ANIP_LOGI("handle %d finish.", handle);
    return ret;
}

CVI_S32 ANIP_SERVICE_Pause(CVI_VOID) {
    g_anip_is_pause = 1;
    while (g_anip_is_pause_ok == 0) {
        g_anip_is_pause = 1;
        usleep(1000);
    }
    return CVI_SUCCESS;
}

CVI_S32 ANIP_SERVICE_Resume(CVI_VOID) {
    g_anip_is_pause = 0;
    return CVI_SUCCESS;
}

CVI_S32 ANIP_SERVICE_Enable(ANIP_SERVICE_HANDLE_T handle, CVI_S32 enable_det, CVI_S32 enable_rec) {
    CVI_S32 ret = CVI_SUCCESS;
    ANIP_CONTEXT_T* pcontext = NULL;
    ANIP_SERVICE_PARAM_S* pparam = NULL;

    if (handle < MAX_ANIP_CNT && handle >= 0) {
        pcontext = &g_anip_context[handle];
        pparam = &pcontext->param;
    } else {
        ANIP_LOGE("handle(%d)", handle);
        return ANIP_FAILURE;
    }

    if (enable_det) {
        pparam->det_enable = 1;
    }
    if (enable_rec) {
        pparam->rec_enable = 1;
    }

    if (!pcontext->is_running) {
        ret = anip_start(pcontext);
    }

    return ret;
}

CVI_S32 ANIP_SERVICE_Disable(ANIP_SERVICE_HANDLE_T handle, CVI_S32 disable_det, CVI_S32 disable_rec) {
    CVI_S32 ret = CVI_SUCCESS;
    ANIP_CONTEXT_T* pcontext = NULL;
    ANIP_SERVICE_PARAM_S* pparam = NULL;

    if (handle < MAX_ANIP_CNT && handle >= 0) {
        pcontext = &g_anip_context[handle];
        pparam = &pcontext->param;
    } else {
        ANIP_LOGE("handle(%d)", handle);
        return ANIP_FAILURE;
    }

    if (disable_det) {
        pparam->det_enable = 0;
    }
    if (disable_rec) {
        pparam->rec_enable = 0;
    }

    if (pparam->det_enable == 0 && pparam->rec_enable == 0 && pcontext->is_running) {
        ret = anip_stop(pcontext);
    }

    return ret;
}

CVI_S32 ANIP_SERVICE_Register_DrawRects_Callback(AnipDrawRectFun pfun) {
    if (pfun != NULL) {
        g_draw_rect_func = pfun;
        return ANIP_SUCCESS;
    }
    return ANIP_FAILURE;
}

CVI_S32 ANIP_SERVICE_Unregister_DrawRects_Callback(CVI_VOID) {
    g_draw_rect_func = NULL;
    return ANIP_SUCCESS;
}

CVI_S32 ANIP_SERVICE_Register_Result_Callback(AnipResultFun pfun) {
    if (pfun != NULL) {
        g_result_func = pfun;
        return ANIP_SUCCESS;
    }
    return ANIP_FAILURE;
}

CVI_S32 ANIP_SERVICE_Unregister_Result_Callback(CVI_VOID) {
    g_result_func = NULL;
    return ANIP_SUCCESS;
}

CVI_S32 ANIP_SERVICE_Clear_Rects(ANIP_SERVICE_HANDLE_T handle) {
    ANIP_CONTEXT_T* pcontext = NULL;
    ANIP_SERVICE_PARAM_S* pparam = NULL;

    if (handle < MAX_ANIP_CNT && handle >= 0) {
        pcontext = &g_anip_context[handle];
        pparam = &pcontext->param;
        anip_clear_rect(pparam->osd_id);
    }
    return ANIP_SUCCESS;
}

CVI_U32 ANIP_SERVICE_Get_Rects(RECT_S* rects, CVI_U32 max_count) {
    if (rects == NULL || max_count == 0) {
        return 0;
    }
    CVI_U32 copy_cnt = (max_count < MAX_ANIMAL_CNT) ? max_count : MAX_ANIMAL_CNT;
    memcpy(rects, g_anip_rects, sizeof(RECT_S) * copy_cnt);
    return copy_cnt;
}
