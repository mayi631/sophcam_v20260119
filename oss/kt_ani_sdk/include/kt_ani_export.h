// kt_pet_export.h
#pragma once

// 定义API可见性宏
#ifdef KT_CAT_BUILD_SHARED_LIB
    #define KT_CAT_API __attribute__((visibility("default")))
    #define KT_CAT_INTERNAL_API __attribute__((visibility("hidden")))
#else
    #define KT_CAT_API
    #define KT_CAT_INTERNAL_API
#endif