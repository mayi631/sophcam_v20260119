// kt_pet_log.h
#ifndef KT_PET_LOG_H
#define KT_PET_LOG_H
#include <stdio.h>
#include <string.h>
#include <stdarg.h> 
#include "kt_ani_export.h"


// ------------------------
// 日志级别定义
// ------------------------
#define SDK_LOG_LEVEL_NONE    0
#define SDK_LOG_LEVEL_ERROR   1
#define SDK_LOG_LEVEL_WARN    2
#define SDK_LOG_LEVEL_INFO    3
#define SDK_LOG_LEVEL_DEBUG   4


// 默认编译保留级别（可被 -DSDK_LOG_COMPILE_LEVEL=... 覆盖）
#ifndef SDK_LOG_COMPILE_LEVEL
    #define SDK_LOG_COMPILE_LEVEL SDK_LOG_LEVEL_INFO
#endif

// ============================================================================
// 编译时日志使能开关（基于 SDK_LOG_COMPILE_LEVEL）
// ============================================================================
#if SDK_LOG_COMPILE_LEVEL >= SDK_LOG_LEVEL_DEBUG
    #define SDK_ENABLE_LOG_DEBUG  1
#else
    #define SDK_ENABLE_LOG_DEBUG  0
#endif

#if SDK_LOG_COMPILE_LEVEL >= SDK_LOG_LEVEL_INFO
    #define SDK_ENABLE_LOG_INFO   1
#else
    #define SDK_ENABLE_LOG_INFO   0
#endif

// WARN 和 ERROR 通常总是保留
#define SDK_ENABLE_LOG_WARN   1
#define SDK_ENABLE_LOG_ERROR  1

// ============================================================================
// 提取短文件名（跨平台）
// ============================================================================
#ifdef _WIN32
    #define __KT_FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#else
    #define __KT_FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif


// ============================================================================
// 外部声明：由 kt_pet_log.c 定义
// ============================================================================
extern KT_CAT_API int g_sdk_runtime_log_level;
extern KT_CAT_API int g_sdk_log_enabled;
extern KT_CAT_API FILE* g_log_fp;
extern KT_CAT_API long g_log_pos;
extern char g_log_filename[128];      // 新增：运行时存储文件名
extern long g_log_max_size;           // 新增：运行时存储最大大小
void kt_pet_log_write_internal(
    const char* level_str,
    const char* filename,
    int line,
    const char* func,
    int do_flush,
    const char* fmt,
    ...);
// ------------------------
//  日志宏
// ------------------------
#if SDK_ENABLE_LOG_DEBUG
    #define SDK_LOGD(fmt, ...) \
        do { \
            if (g_sdk_log_enabled && g_sdk_runtime_log_level >= SDK_LOG_LEVEL_DEBUG) { \
                kt_pet_log_write_internal("SDK-D", __KT_FILENAME__, __LINE__, __func__, 0, fmt, ##__VA_ARGS__); \
            } \
        } while(0)
#else
    #define SDK_LOGD(fmt, ...) ((void)0)
#endif

#if SDK_ENABLE_LOG_INFO
    #define SDK_LOGI(fmt, ...) \
        do { \
            if (g_sdk_log_enabled && g_sdk_runtime_log_level >= SDK_LOG_LEVEL_INFO) { \
                kt_pet_log_write_internal("SDK-I", __KT_FILENAME__, __LINE__, __func__, 0, fmt, ##__VA_ARGS__); \
            } \
        } while(0)
#else
    #define SDK_LOGI(fmt, ...) ((void)0)
#endif


#define SDK_LOGW(fmt, ...) \
    do { \
        if (g_sdk_log_enabled && g_sdk_runtime_log_level >= SDK_LOG_LEVEL_WARN) { \
            kt_pet_log_write_internal("SDK-W", __KT_FILENAME__, __LINE__, __func__, 0, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define SDK_LOGE(fmt, ...) \
    do { \
        if (g_sdk_log_enabled && g_sdk_runtime_log_level >= SDK_LOG_LEVEL_ERROR) { \
            kt_pet_log_write_internal("SDK-E", __KT_FILENAME__, __LINE__, __func__, 1, fmt, ##__VA_ARGS__); \
        } \
    } while(0)


// ============================================================================
// API 声明
// ============================================================================

KT_CAT_API void sdk_log_init(void);
KT_CAT_API void sdk_log_init_with_config(const char* log_file, long max_size);
KT_CAT_API void sdk_log_deinit(void);
KT_CAT_API void sdk_log_flush(void);
KT_CAT_API void sdk_set_log_level(int level);
KT_CAT_API void sdk_set_log_enable(int enable);
KT_CAT_API void sdk_log_resize(long new_max_size); // 动态调整日志文件大小限制

#endif // KT_PET_LOG_H
