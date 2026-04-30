/*
 * 软件版本信息页面
 */

#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "page_all.h"
#include "style_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// 页面全局变量
lv_obj_t *obj_sysMenu_version_s = NULL;
static lv_obj_t *cont_top              = NULL;
static lv_obj_t *btn_back              = NULL;
static lv_obj_t *title                 = NULL;
static lv_obj_t *cont_content          = NULL;
extern lv_style_t ttf_font_20;
extern lv_style_t ttf_font_16;
// 版本信息结构
typedef struct
{
    const char *name;
    const char *version;
} version_info_t;
static char sys_version[50];

// 示例版本信息
static version_info_t version_info[] = {
    {NULL/*"主程序"*/, MAIN_VERSION " " SOPHCAM_COMMIT_ID},
    {NULL/*"系统内核"*/, "NULL"},
    {NULL/*"设备型号"*/, DEVICE_MODEL_CODE},
    {NULL/*"编译时间"*/, __DATE__ " " __TIME__},
};

static void sysmenu_version_menu_callback(int key_code, int key_value);


// 更新指定语言的版本信息
static int32_t update_version_info(uint8_t language) {
    if(language > NUM_LANGUAGES) {
        return -1;
    }

    /* 需要注意对应关系是写死的 */
    version_info[0].name = str_language_main_program[language];
    version_info[1].name = str_language_system_kernel[language];
    version_info[2].name = str_language_device_model[language];
    version_info[3].name = str_language_build_time[language];
    return 0;
}

// 返回按钮事件处理
static void version_info_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CLICKED) {
        // 返回系统设置页面
        ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                              false, true);
    }
}

// get sys version
static char * get_sys_version(char * version, size_t version_size)
{
    if (version_size < 2) {
        return strdup("ERROR");
    }
    FILE *f = fopen("/proc/version", "r");
    if (!f) {
        MLOG_ERR("open /proc/version fail\n");
        return strdup("ERROR");
    }

    if (!version) {
        fclose(f);
        MLOG_ERR("version null\n");
        return strdup("ERROR");
    }

    if (!fgets(version, (int)version_size, f)) {
        fclose(f);
        MLOG_ERR("read version fail\n");
        return strdup("ERROR");
    }
    fclose(f);
    version[strcspn(version, "\n")] = '\0';
    return version;
}
// 手势事件处理
static void version_info_gesture_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if(dir == LV_DIR_RIGHT) {
            // 右滑返回
            ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                  false, true);
        }
    }
}

// 创建版本信息页面
void version_info_page(lv_ui_t *ui)
{
    MLOG_DBG("创建版本信息页面\n");

    // 清理旧页面
    if(obj_sysMenu_version_s != NULL) {
        if(lv_obj_is_valid(obj_sysMenu_version_s)) {
            lv_obj_del(obj_sysMenu_version_s);
        }
        obj_sysMenu_version_s = NULL;
    }

    // 创建新页面
    obj_sysMenu_version_s = lv_obj_create(NULL);
    lv_obj_set_size( obj_sysMenu_version_s , H_RES, V_RES);
    lv_obj_add_style( obj_sysMenu_version_s , &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj_sysMenu_version_s, version_info_gesture_handler, LV_EVENT_GESTURE, ui);

    // 创建顶部栏
    cont_top = lv_obj_create(obj_sysMenu_version_s);
    lv_obj_set_size(cont_top, H_RES, 60);
    lv_obj_set_pos(cont_top, 0, 0);
    lv_obj_set_scrollbar_mode(cont_top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    // 返回按钮
    btn_back = lv_btn_create(cont_top);
    lv_obj_set_size(btn_back, 60, 52);
    lv_obj_set_pos(btn_back, 4, 4);
    lv_obj_add_style(btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, version_info_back_event_handler, LV_EVENT_CLICKED, ui);

    // 返回按钮图标
    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_add_style(label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_back, LV_ALIGN_CENTER, 0, 0);

    // 标题
    title = lv_label_create(cont_top);
    lv_label_set_text(title, str_language_version_info[get_curr_language()]);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 内容容器
    cont_content = lv_obj_create(obj_sysMenu_version_s);
    lv_obj_set_size(cont_content, 600, 280);
    lv_obj_set_pos(cont_content, 20, 80);
    lv_obj_set_style_bg_opa(cont_content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_content, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont_content, 20, LV_PART_MAIN | LV_STATE_DEFAULT);

    update_version_info(get_curr_language());

    // 创建版本信息列表
    for(uint8_t i = 0; i < sizeof(version_info) / sizeof(version_info[0]); i++) {

        // update sys verison
        if (strcmp(version_info[i].name, str_language_system_kernel[get_curr_language()]) == 0) {
            get_sys_version(sys_version, 40);
            version_info[i].version = sys_version;
        }

        // 创建行容器
        lv_obj_t *row = lv_obj_create(cont_content);
        lv_obj_set_size(row, 560, 50);
        lv_obj_set_pos(row, 0, i * 60);
        lv_obj_set_style_bg_opa(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        // 模块名称
        lv_obj_t *label_name = lv_label_create(row);
        lv_label_set_text(label_name, version_info[i].name);
        lv_obj_set_style_text_color(label_name, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_style(label_name, &ttf_font_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(label_name, LV_ALIGN_LEFT_MID, 0, 0);

        // 版本号
        lv_obj_t *label_version = lv_label_create(row);
        lv_label_set_text(label_version, version_info[i].version);
        lv_obj_set_style_text_color(label_version, lv_color_hex(0xCCCCCC), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label_version, &lv_font_SourceHanSerifSC_Regular_20,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(label_version, LV_ALIGN_RIGHT_MID, 0, 0);

        // 添加分隔线（最后一行不加）
        if(i < sizeof(version_info) / sizeof(version_info[0]) - 1) {
            lv_obj_t *line                          = lv_line_create(cont_content);
            static lv_point_precise_t line_points[] = {{0, 0}, {560, 0}};
            lv_line_set_points(line, line_points, 2);
            lv_obj_set_style_line_width(line, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_line_color(line, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_pos(line, 0, (i + 1) * 60 - 5);
        }
    }

    // 添加公司信息
    lv_obj_t *company_label = lv_label_create(obj_sysMenu_version_s);
    lv_label_set_text(company_label, "Copyright © 2025 SOPHGO");
    lv_obj_set_style_text_color(company_label, lv_color_hex(0x888888), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_style(company_label, &ttf_font_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(company_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    // 更新布局
    lv_obj_update_layout(obj_sysMenu_version_s);

    // 设置当前页面
    set_current_page_handler(sysmenu_version_menu_callback); // 禁用按键处理
}

// 清理版本信息页面资源
void version_info_cleanup(void)
{
    if(obj_sysMenu_version_s != NULL) {
        if(lv_obj_is_valid(obj_sysMenu_version_s)) {
            lv_obj_del(obj_sysMenu_version_s);
        }
        obj_sysMenu_version_s = NULL;
    }
}

// 获取版本信息页面
lv_obj_t *get_obj_sysMenu_version_seen(void)
{
    return obj_sysMenu_version_s;
}

static void sysmenu_version_menu_callback(int key_code, int key_value)
{
    static uint32_t first_press_time = 0; // 第一次按键时间
    static uint32_t last_press_time = 0; // 上次按键时间
    static uint8_t press_count = 0; // 连续按键计数

    if (key_code == KEY_MENU && key_value == 1) {
        ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
            false, true);
    }
    else if (key_code == KEY_DOWN && key_value == 1) {
        uint32_t current_time = lv_tick_get();

        // 如果是第一次按下，记录起始时间
        if (press_count == 0) {
            first_press_time = current_time;
            last_press_time = current_time;
            press_count = 1;
            MLOG_DBG("开始计数，当前第1次\n");
        } else {
            // 检查总时间是否超过3秒
            if (current_time - first_press_time > 3000) {
                // 超过3秒，重新开始计数
                press_count = 1;
                first_press_time = current_time;
                last_press_time = current_time;
                MLOG_DBG("总时间超过3秒，重新开始计数，当前第1次\n");
            }
            // 检查相邻按键间隔是否超过500ms（更严格的间隔）
            else if (current_time - last_press_time > 500) {
                // 相邻按键间隔超过500ms，重新开始计数
                press_count = 1;
                first_press_time = current_time;
                last_press_time = current_time;
                MLOG_DBG("按键间隔超过500ms，重新开始计数，当前第1次\n");
            } else {
                // 符合条件，计数增加
                press_count++;
                last_press_time = current_time;
                MLOG_DBG("音量下键连续按下 %d 次，总时间: %dms，上次间隔: %dms\n",
                    press_count, current_time - first_press_time, current_time - last_press_time);
            }
        }

        // 检查是否达到5次
        if (press_count >= 5) {
            MLOG_DBG("3秒内连续按下5次 W 按键，开启ADB调试\n");
            system("/etc/init.d/P10adbd restart");

            // 重置计数器
            press_count = 0;
            first_press_time = 0;
            last_press_time = 0;
        }
    }
}