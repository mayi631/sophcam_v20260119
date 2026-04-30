#ifndef ANIMAL_LABELS_H
#define ANIMAL_LABELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 动物信息结构体 */
typedef struct {
    int id;                 /* 动物ID */
    const char *species_en; /* 英文名 */
    const char *species_sci;/* 科学名(拉丁名) */
    const char *species_cn; /* 中文名 */
} animal_info_t;

/* 获取动物信息数组 */
const animal_info_t *get_animal_info_array(void);

/* 获取动物信息数量 */
int get_animal_info_count(void);

/* 根据 ID 获取动物名称(根据当前语言环境返回中文名或英文名) */
const char *get_animal_name_by_id(int id);

#ifdef __cplusplus
}
#endif

#endif /* ANIMAL_LABELS_H */
