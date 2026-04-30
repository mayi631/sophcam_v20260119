// kt_ani_api.h

#ifndef KT_ANI_API_H
#define KT_ANI_API_H

#include "kt_ani_export.h"
#include "kt_ani_type.h"
#include <stddef.h>  // for size_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 初始化宠物芯片识别系统。与 kt_ani_destroy 成对使用。
 * @param read_only_dir 这个是放模型文件的目录（.om)。可选，传NULL即可。
 * @param data_dir 数据目录，可选，传NULL即可。
 * @param config_dir 这个是授权文件，包里自带：user_config.json，授权时生成：run.json，可选，传NULL即可。
 * @return 返回初始化状态码。
 */
KT_CAT_API KTAniError kt_ani_init(const char* read_only_dir,
                                  const char* data_dir,
                                  const char* config_dir);

/**
 * @brief 销毁宠物芯片识别系统资源。与 kt_ani_init 成对使用。
 */
KT_CAT_API void kt_ani_destroy();

/**
 * @brief 执行宠物识别任务。
 * @param img_path 图像文件路径。
 * @param task_type 任务类型。
 * @param result 结果输出缓冲区。
 * @param result_count 结果数量。
 * @return 返回任务执行状态码。
 */
KT_CAT_API KTAniError kt_ani_task(const char* img_path, KTAniTaskType task_type, struct KTAniInfo* result, int* result_count);

/**
 * @brief 从内存中执行宠物识别任务。
 * @param frame_info 帧信息。
 * @param task_type 任务类型。
 * @param result 结果输出缓冲区。
 * @param result_count 结果数量。
 * @return 返回任务执行状态码。
 */
KT_CAT_API KTAniError kt_ani_task_from_memory(KTFrameInfo* frame_info, KTAniTaskType task_type, struct KTAniInfo* result, int* result_count);


/**
 * @brief 设置宠形检测的灵敏度
 * @param sensitivity 灵敏度，范围0-1，默认0.3
 * @return KT_ANI_OK 表示成功，其他值表示失败
 * @note 灵敏度越高，检测的结果越准确，但会增加检测时间，建议在实际使用时根据实际情况调整灵敏度
 */
KT_CAT_API KTAniError set_detection_Sensitivity(float sensitivity);


/**
 * @brief 获取系统版本号。
 * @return 返回版本号字符串。
 */
KT_CAT_API const char* get_ani_version();


#ifdef __cplusplus
}
#endif

#endif //KT_PET_API_H