#!/usr/bin/env python3
"""
将 animal_labels.json 转换为 C 数组
用法: python3 json_to_c_array.py animal_labels.json output.c output.h
"""

import json
import sys
import os

def json_to_c_array(json_file, output_c, output_h):
    # 读取 JSON 文件
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    # 生成头文件
    header_guard = os.path.basename(output_h).upper().replace('.', '_').replace('-', '_')
    
    h_content = f"""#ifndef {header_guard}
#define {header_guard}

#ifdef __cplusplus
extern "C" {{
#endif

/* 动物信息结构体 */
typedef struct {{
    int id;                 /* 动物ID */
    const char *species_en; /* 英文名 */
    const char *species_sci;/* 科学名(拉丁名) */
    const char *species_cn; /* 中文名 */
}} animal_info_t;

/* 获取动物信息数组 */
const animal_info_t *get_animal_info_array(void);

/* 获取动物信息数量 */
int get_animal_info_count(void);

/* 根据 ID 获取动物名称(根据当前语言环境返回中文名或英文名) */
const char *get_animal_name_by_id(int id);

#ifdef __cplusplus
}}
#endif

#endif /* {header_guard} */
"""
    
    # 生成 C 文件
    c_content = f"""#include "{os.path.basename(output_h)}"
#include <string.h>

/* 动物信息数组 */
static const animal_info_t animal_info_array[] = {{
"""
    
    for item in data:
        item_id = item.get('id', 0)
        species_en = item.get('species_en', '').replace('"', '\\"')
        species_sci = item.get('species_sci', '').replace('"', '\\"')
        species_cn = item.get('species_cn', '').replace('"', '\\"')
        
        c_content += f'    [{item_id}] = {{{item_id}, "{species_en}", "{species_sci}", "{species_cn}"}},\n'
    
    c_content += f"""}};

/* 获取动物信息数组 */
const animal_info_t *get_animal_info_array(void)
{{
    return animal_info_array;
}}

/* 获取动物信息数量 */
int get_animal_info_count(void)
{{
    return sizeof(animal_info_array) / sizeof(animal_info_array[0]);
}}

/* 根据 ID 获取动物名称 */
const char *get_animal_name_by_id(int id)
{{
    if (id < 0 || id >= (int)(sizeof(animal_info_array) / sizeof(animal_info_array[0]))) {{
        static char unknown[32];
        snprintf(unknown, sizeof(unknown), "Unknown(%d)", id);
        return unknown;
    }}
    
    /* 这里可以根据当前语言环境返回不同的名称 */
    /* 默认返回英文名 */
    if (animal_info_array[id].species_en && strlen(animal_info_array[id].species_en) > 0) {{
        return animal_info_array[id].species_en;
    }}
    
    /* 如果英文名为空，返回科学名 */
    if (animal_info_array[id].species_sci && strlen(animal_info_array[id].species_sci) > 0) {{
        return animal_info_array[id].species_sci;
    }}
    
    /* 如果都为空，返回未知 */
    static char unknown[32];
    snprintf(unknown, sizeof(unknown), "Unknown(%d)", id);
    return unknown;
}}
"""
    
    # 写入文件
    with open(output_h, 'w', encoding='utf-8') as f:
        f.write(h_content)
    
    with open(output_c, 'w', encoding='utf-8') as f:
        f.write(c_content)
    
    print(f"成功生成文件:")
    print(f"  - {output_h} ({len(h_content)} 字节)")
    print(f"  - {output_c} ({len(c_content)} 字节)")
    print(f"  - 共 {len(data)} 条动物记录")

if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"用法: {sys.argv[0]} <json_file> <output_c> <output_h>")
        sys.exit(1)
    
    json_file = sys.argv[1]
    output_c = sys.argv[2]
    output_h = sys.argv[3]
    
    json_to_c_array(json_file, output_c, output_h)
