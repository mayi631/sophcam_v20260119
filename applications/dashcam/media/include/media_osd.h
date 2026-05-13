#ifndef _MEDIA_OSD_H_
#define _MEDIA_OSD_H_

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* End of #ifdef __cplusplus */

#include "cvi_type.h"
#include "mapi.h"

int32_t MEDIA_StartOsd(void);
int32_t MEDIA_StopOsd(void);
int32_t MEDIA_UpdateCarNumOsd(void);

/**
 * @brief 绘制矩形框（供人脸/动物识别服务调用）
 * @param osd_id OSD区域ID
 * @param num 矩形数量
 * @param rects 矩形数组
 * @return 0成功，非0失败
 */
CVI_S32 MEDIA_DrawRects(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

#endif /* End of #ifndef _MEDIA_OSD_H_ */