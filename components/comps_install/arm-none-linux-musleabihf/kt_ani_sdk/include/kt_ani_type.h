// kt_ani_type.h

#ifndef KT_ANI_TYPE_H
#define KT_ANI_TYPE_H

#include <stdint.h>
#include <stdbool.h>


// 错误码
typedef enum {
    KT_ANI_OK = 0,                      // 成功
    KT_ANI_ERROR = -1,                  // 通用错误
    KT_ANI_INVALID_INPUT = -2,          // 无效输入参数，参数错误
    KT_ANI_MODEL_LOAD_FAILED = -3,      // 模型加载失败
    KT_ANI_DET_FAILED = -4,             // 目标检测异常
    KT_ANI_REC_FAILED = -5,             // 动物识别异常
    KT_ANI_NO_OBJ_FOUND = -6,           // 未检测到目标
    KT_ANI_BUFFER_TOO_SMALL = -7,       // 结果缓冲区太小，无法容纳所有结果
    KT_ANI_NOT_INIT = -8,               // 无效的引擎句柄
    KT_ANI_LOG_INIT_FAILED = -9,        // 日志系统初始化失败
    KT_ANI_LICENSE_INVALID = -101,      // 非法的授权信息
    KT_ANI_LICENSE_ONLINE_FAILED = -102,// 在线授权失败
    KT_ANI_NO_AUTH_INTERFACE = -103,    // 该接口未授权
    KT_ANI_END = -10000,                // 结束标识
} KTAniError;

// 任务类型
typedef enum {
    KT_TASK_DET_ANI = 1,     // 目标检测（包括人、车、动物）
    KT_TASK_REC_ANI = 2,     // 动物识别（1000+种动物名称）
} KTAniTaskType;


/**
 * @brief 边界框结构体，用于表示检测到的目标位置
 * 左上角x1 = x - width/2
 * 左上角y1 = y - height/2
 * 右下角x2 = x + width/2
 * 右下角y2 = y + height/2
 */
struct KTBoundingBox {
    bool valid;
    int cls_idx;   // -1: 未初始化、0: animal、1: person、2: vehicle
    float x;
    float y;
    float width;
    float height;
    float confidence;
};


/**
 * @brief 动物信息结构体，用于表示检测到的目标位置及类别信息
 */
struct KTAniInfo {
    bool valid;
    int ani_idx;   // -1: 未初始化，1000+种动物类别索引
    float confidence;
    struct KTBoundingBox bbox;
};


/**
 * @brief 输入数据类型
 */
typedef enum {
    KT_PIXEL_FORMAT_RGB,    // 支持RGB，HWC格式
    KT_PIXEL_FORMAT_NV12,   // 支持NV12， 420格式
} KTPixelFormat;


/**
 * @brief 图像帧信息结构体，用于表示图像数据, 支持多平面格式，如RGB、NV12等
 */
typedef struct {
    uint8_t* data;        // yuv-data or rgb data
    int width;            // 表示图像的宽度（以像素为单位）
    int height;           // 表示图像的高度（以像素为单位）
    KTPixelFormat format; // 指定此图像帧使用的像素格式，通过 KTPixelFormat 枚举值来表示。
} KTFrameInfo;


#endif // KT_ANI_TYPE_H