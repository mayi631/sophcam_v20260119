/**
 * @file anip_service.h
 * @brief 动物识别服务接口
 *
 * 完全参考 facep_service.h 的架构
 */
#ifndef __ANIP_SERVICE_H__
#define __ANIP_SERVICE_H__

#include "cvi_type.h"
#include "mapi.h"
#include "kt_ani_api.h"

/* 最大支持的同时检测的动物数量 */
#define MAX_ANIMAL_CNT 32

/* 动物识别服务句柄类型 */
typedef CVI_S32 ANIP_SERVICE_HANDLE_T;

/* 动物识别结果 */
typedef struct _ANIP_RESULT_S {
    CVI_CHAR name[64];         /* 识别名称 */
    CVI_S32  cls_idx;          /* 类别索引: 0=动物, 1=人, 2=车辆 */
} ANIP_RESULT_S;

/* 动物检测框回调函数类型 */
typedef CVI_S32 (*AnipDrawRectFun)(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects);

/* 动物识别结果回调函数类型（返回动物名称） */
typedef CVI_VOID (*AnipResultFun)(CVI_U32 osd_id, ANIP_RESULT_S* results, CVI_U32 count);

/* 动物识别服务参数结构体 */
typedef struct _ANIP_SERVICE_PARAM_S {
    CVI_S32 in_vpss_grp;        /* VPSS Group ID */
    CVI_S32 in_vpss_chn;        /* VPSS Channel ID */
    CVI_U32 in_width;           /* 输入图像宽度 */
    CVI_U32 in_height;          /* 输入图像高度 */
    CVI_U32 osd_id;            /* OSD区域ID */
    CVI_U32 osd_mirror;        /* 是否镜像 */

    /* 功能开关 */
    CVI_S32 det_enable;         /* 检测功能开关 */
    CVI_S32 rec_enable;         /* 识别功能开关 */

    /* 检测参数 */
    CVI_U32 sensitivity;         /* 检测灵敏度 */
    CVI_U32 max_results;        /* 最大检测结果数 */
} ANIP_SERVICE_PARAM_S;

/* 动物识别服务错误码 */
typedef enum {
    ANIP_SUCCESS = 0,
    ANIP_FAILURE = -1,
    ANIP_NOT_INIT = -2,
    ANIP_INVALID_PARAM = -3,
    ANIP_ENGINE_ERROR = -4,
    ANIP_NO_MEMORY = -5,
} ANIP_ERROR_E;

/**
 * @brief 创建动物识别服务
 */
CVI_S32 ANIP_SERVICE_Create(ANIP_SERVICE_HANDLE_T* phandle, ANIP_SERVICE_PARAM_S* pparam);

/**
 * @brief 销毁动物识别服务
 */
CVI_S32 ANIP_SERVICE_Destroy(ANIP_SERVICE_HANDLE_T handle);

/**
 * @brief 暂停动物识别服务
 */
CVI_S32 ANIP_SERVICE_Pause(CVI_VOID);

/**
 * @brief 恢复动物识别服务
 */
CVI_S32 ANIP_SERVICE_Resume(CVI_VOID);

/**
 * @brief 启用动物识别功能
 */
CVI_S32 ANIP_SERVICE_Enable(ANIP_SERVICE_HANDLE_T handle, CVI_S32 enable_det, CVI_S32 enable_rec);

/**
 * @brief 禁用动物识别功能
 */
CVI_S32 ANIP_SERVICE_Disable(ANIP_SERVICE_HANDLE_T handle, CVI_S32 disable_det, CVI_S32 disable_rec);

/**
 * @brief 注册绘制矩形框回调函数
 */
CVI_S32 ANIP_SERVICE_Register_DrawRects_Callback(AnipDrawRectFun pfun);

/**
 * @brief 注销绘制矩形框回调函数
 */
CVI_S32 ANIP_SERVICE_Unregister_DrawRects_Callback(CVI_VOID);

/**
 * @brief 注册动物识别结果回调函数
 */
CVI_S32 ANIP_SERVICE_Register_Result_Callback(AnipResultFun pfun);

/**
 * @brief 注销动物识别结果回调函数
 */
CVI_S32 ANIP_SERVICE_Unregister_Result_Callback(CVI_VOID);

/**
 * @brief 清除所有检测框
 */
CVI_S32 ANIP_SERVICE_Clear_Rects(ANIP_SERVICE_HANDLE_T handle);

/**
 * @brief 获取当前检测框的位置信息
 * @param rects 输出参数，存储检测框数组
 * @param max_count 输入参数，最大支持的框数量
 * @return 实际检测到的框数量
 */
CVI_U32 ANIP_SERVICE_Get_Rects(RECT_S* rects, CVI_U32 max_count);

#endif /* __ANIP_SERVICE_H__ */
