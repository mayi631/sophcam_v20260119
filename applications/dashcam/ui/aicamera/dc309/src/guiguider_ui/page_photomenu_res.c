/*
 * Copyright 2025 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */
#define DEBUG
#include "common/takephoto.h"
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "lvgl.h"
#include "page_all.h"
#include "style_common.h"
#include "ui_common.h"
#include <stdio.h>

#define GRID_COLS 1
#define GRID_ROWS 7
#define GRID_MAX_OBJECTS GRID_ROWS * GRID_COLS

static lv_obj_t *focusable_objects[GRID_MAX_OBJECTS];
char g_button_labelRes[32] = {0};
static uint8_t res_Current_Index_s = 0;

/* 分辨率图标资源数组 */
static const res_to_icon_t photo_Res_Icons[] = {
    // {.w = 8192, .h = 8192, .icon_c = "64M.png"},
    {.w = 8192, .h = 6144, .icon_c = "48M.png"},
    {.w = 8064, .h = 4536, .icon_c = "36M.png"},
    {.w = 7680, .h = 4320, .icon_c = "32M.png"},
    // {.w = 5600, .h = 4200, .icon_c = "24M.png"},*
    {.w = 5760, .h = 3240, .icon_c = "16M.png"},
    // {.w = 4000, .h = 3000, .icon_c = "12M.png"},
    {.w = 3840, .h = 2160, .icon_c = "8M.png"},
    {.w = 2688, .h = 1512, .icon_c = "4M.png"},
    {.w = 1920, .h = 1080, .icon_c = "2M.png"},
    // {.w = 1920, .h = 1080, .icon_c = "2m.png"},
    // {.w = 640, .h = 480, .icon_c = "vga2.png"},
};

uint8_t photo_getRes_Index(void)
{
    return res_Current_Index_s / 2;
}

void photo_setRes_Index(uint8_t index)
{
    res_Current_Index_s = index * 2;
}

void photo_setRes_Label(const char* plabel)
{
    memset(g_button_labelRes, 0, sizeof(g_button_labelRes));
    strncpy(g_button_labelRes, plabel, sizeof(g_button_labelRes));
}

char* photo_getRes_Label(void)
{
    return g_button_labelRes;
}

char* photo_getRes_Icon(void)
{
    int32_t width = 0;
    uint8_t index = 0;
    int32_t icons_num = 0;
    int32_t i = 0;
    PARAM_MENU_S menu_param = {0};
    PARAM_GetMenuParam(&menu_param);
    index = photo_getRes_Index();
    if(index >= menu_param.PhotoSize.ItemCnt){
        MLOG_ERR("over\n");
        return NULL;
    }
    width = menu_param.PhotoSize.Items[index].Value;
    icons_num = sizeof(photo_Res_Icons) / sizeof(res_to_icon_t);
    for(i = 0; i < icons_num; i++){
        if(width == photo_Res_Icons[i].w){
            return photo_Res_Icons[i].icon_c;
        }
    }
    MLOG_ERR("no icon found with width %d\n", width);
    return NULL;
}

// 根据宽度获取图标索引
int get_photo_res_icon_index_by_width(int width)
{
    int icons_num = sizeof(photo_Res_Icons) / sizeof(res_to_icon_t);
    for (int i = 0; i < icons_num; i++) {
        if (width == photo_Res_Icons[i].w) {
            return i;
        }
    }
    return -1;  // 未找到
}

// 根据索引获取分辨率图标
const char* photo_getRes_IconByIndex(uint8_t index)
{
    int icons_num = sizeof(photo_Res_Icons) / sizeof(res_to_icon_t);
    if (index >= icons_num) {
        return "8M.png";  // 默认图标
    }
    return photo_Res_Icons[index].icon_c;
}

static void res_Del_Complete_anim_cb(lv_anim_t *a)
{
    if(g_ui.page_resolution.resscr != NULL) {
        if(lv_obj_is_valid(g_ui.page_resolution.resscr)) {
            lv_obj_del(g_ui.page_resolution.resscr);
        } else {
        }
        g_ui.page_resolution.resscr = NULL;
        ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                              &g_ui.screen_SettingResolution_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                              true);
    }
}

static void res_win_Delete_anim(void)
{
    lv_anim_t Delete_anim; //动画渐隐句柄
    // 创建透明度动画
    lv_anim_init(&Delete_anim);
    lv_anim_set_values(&Delete_anim, 0, 1);

    lv_anim_set_time(&Delete_anim, 6);

    // lv_anim_set_exec_cb(&Delete_anim, AIanim_objSet_Opa);
    lv_anim_set_path_cb(&Delete_anim, lv_anim_path_ease_out);
    // 设置动画完成回调（销毁对象）
    lv_anim_set_completed_cb(&Delete_anim, res_Del_Complete_anim_cb);

    lv_anim_start(&Delete_anim);
}

static void screen_SettingResolution_btn_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                                  &g_ui.screen_SettingResolution_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                  false, true);
            break;
        }
        default: break;
    }
}

void events_init_screen_SettingResolution(lv_ui_t *ui)
{
    lv_obj_add_event_cb(ui->page_resolution.btn_back, screen_SettingResolution_btn_back_event_handler, LV_EVENT_CLICKED,
                        ui);
}

void photoRes_SelectFocus_OK(lv_event_t *e, lv_obj_t *obj)
{
    lv_obj_t *btn_clicked = NULL;
    if(obj == NULL) {
        btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
    } else {
        btn_clicked = obj;
    }
    lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(parent, res_Current_Index_s);

    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if(i == res_Current_Index_s) {
            //先设置焦点控件,再进行滚动,否则会直接滚动到最下,不知什么原因.
            lv_group_focus_obj(chlid);
            lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
            //设置焦点渐变
            lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
            //设置焦点BG
            lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            //设置焦点标签颜色
            lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xFFFFFF),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN |
            // LV_STATE_DEFAULT);

        } else {
            lv_obj_t *other_child = lv_obj_get_child(parent, i);
            //设置焦点渐变
            lv_set_obj_grad_style(other_child, LV_GRAD_DIR_VER, lv_color_hex(0), lv_color_hex(0));
            //设置焦点BG
            lv_obj_set_style_bg_color(other_child, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        if((btn_clicked == lv_obj_get_child(parent, i))) {
            if(lv_obj_get_child(btn_clicked, 1) == NULL) {
                lv_obj_t *label1 = lv_label_create(btn_clicked);
                lv_obj_set_style_text_color(label1, lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(label1, "" LV_SYMBOL_OK " ");
                lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);
                lv_obj_align(label1, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        } else {
            lv_obj_t *child = lv_obj_get_child(parent, i);
            if(lv_obj_get_child(child, 1) != NULL) {
                lv_obj_del(lv_obj_get_child(child, 1));
            }
        }
    }
}

static void screen_photoResolution_btn_event_handler(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e);
    MESSAGE_S event = {0};
    uint8_t j = 0;
    switch(code) {
        case LV_EVENT_CLICKED: {
            lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
                if(lv_obj_get_child(parent, i) == btn_clicked) {
                    res_Current_Index_s = i;

                    photoRes_SelectFocus_OK(e, NULL);
                    // 获取按钮标签文本
                    lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
                    if(label && lv_obj_check_type(label, &lv_label_class)) {
                        const char *txt = lv_label_get_text(label);
                        PARAM_MENU_S menu_param = {0};
                        PARAM_GetMenuParam(&menu_param);
                        if(txt) strncpy(g_button_labelRes, txt, sizeof(g_button_labelRes));
                            MLOG_INFO("The camera resolution will be switched to  %s\n", g_button_labelRes);
                            event.topic = EVENT_MODEMNG_SETTING;
                            event.arg1 = PARAM_MENU_PHOTO_SIZE;
                            for(j = 0; j < menu_param.PhotoSize.ItemCnt; j++)
                            {
                                if(strcmp(menu_param.PhotoSize.Items[j].Desc, g_button_labelRes) == 0)
                                {
                                    event.arg2 = j;
                                    break;
                                }
                            }
                            if(j == menu_param.PhotoSize.ItemCnt) {
                                MLOG_ERR("don't support %s\n", g_button_labelRes);
                                continue;
                            }else{
                                MLOG_INFO("SWITCH SUCCES %d\n", event.arg2);
                                MODEMNG_SendMessage(&event);
                                set_zoom_level(1);
                            }

                    }
                    lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                    lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000), LV_PART_MAIN);
                } else {
                    lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                    lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
                }
            }
            res_win_Delete_anim();
            break;
        }
        default: break;
    }
}

static void photoRes_click_callback(lv_obj_t *obj)
{
    MLOG_DBG("photoRes_click_callback\n");
    MESSAGE_S event = {0};
    uint8_t j = 0;
    lv_obj_t *parent      = lv_obj_get_parent(obj); //获取发生点击事件的父控件
    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if(lv_obj_get_child(parent, i) == obj) {
            res_Current_Index_s = i;

            photoRes_SelectFocus_OK(NULL, obj);
            // 获取按钮标签文本
            lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
            if(label && lv_obj_check_type(label, &lv_label_class)) {
                const char *txt = lv_label_get_text(label);
                PARAM_MENU_S menu_param = {0};
                PARAM_GetMenuParam(&menu_param);
                if(txt) strncpy(g_button_labelRes, txt, sizeof(g_button_labelRes));
                    MLOG_INFO("The camera resolution will be switched to  %s\n", g_button_labelRes);
                    event.topic = EVENT_MODEMNG_SETTING;
                    event.arg1 = PARAM_MENU_PHOTO_SIZE;
                    for(j = 0; j < menu_param.PhotoSize.ItemCnt; j++)
                    {
                        if(strcmp(menu_param.PhotoSize.Items[j].Desc, g_button_labelRes) == 0)
                        {
                            event.arg2 = j;
                            break;
                        }
                    }
                    if(j == menu_param.PhotoSize.ItemCnt) {
                        MLOG_ERR("don't support %s\n", g_button_labelRes);
                        continue;
                    }else{
                        MLOG_INFO("SWITCH SUCCES %d\n", event.arg2);
                        MODEMNG_SendMessage(&event);
                    }

            }
            lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000), LV_PART_MAIN);
        } else {
            lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        }
    }
    res_win_Delete_anim();
}

static void photoRes_menu_callback(void)
{
    MLOG_DBG("photoRes_menu_callback\n");
    ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                            &g_ui.screen_SettingResolution_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0,
                            0, false, true);
}

static void gesture_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_GESTURE: {
            // 获取手势方向，需要 TP 驱动支持
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            switch(dir) {
                case LV_DIR_RIGHT: {
                    ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                                          &g_ui.screen_SettingResolution_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE,
                                          0, 0, false, true);
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

void menuSetting_Resolution(lv_ui_t *ui)
{

    MLOG_DBG("loading page_resolution...\n");

    SettingResolution_t *SettingResolution = &ui->page_resolution;
    SettingResolution->resdel              = true;
// casnnca
    // 创建主页面1 容器
    if(SettingResolution->resscr != NULL) {
        if(lv_obj_is_valid(SettingResolution->resscr)) {
            MLOG_DBG("page_resolution->resscr 仍然有效，删除旧对象\n");
            lv_obj_del(SettingResolution->resscr);
        } else {
            MLOG_DBG("page_resolution->resscr 已被自动销毁，仅重置指针\n");
        }
        SettingResolution->resscr = NULL;
    }

    // Write codes resscr
    SettingResolution->resscr = lv_obj_create(NULL);
    lv_obj_set_size(SettingResolution->resscr, H_RES, V_RES);
    lv_obj_add_style(SettingResolution->resscr, &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(SettingResolution->resscr, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // Write codes cont_top (顶部栏)
    SettingResolution->cont_top = lv_obj_create(SettingResolution->resscr);
    lv_obj_set_pos(SettingResolution->cont_top, 0, 0);
    lv_obj_set_size(SettingResolution->cont_top, H_RES, 60);
    lv_obj_set_scrollbar_mode(SettingResolution->cont_top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(SettingResolution->cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes btn_back (返回按钮)
    SettingResolution->btn_back = lv_button_create(SettingResolution->cont_top);
    lv_obj_set_pos(SettingResolution->btn_back, 4, 4);
    lv_obj_set_size(SettingResolution->btn_back, 60, 52);
    lv_obj_add_style(SettingResolution->btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    SettingResolution->label_back = lv_label_create(SettingResolution->btn_back);
    lv_label_set_text(SettingResolution->label_back, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(SettingResolution->label_back, LV_LABEL_LONG_WRAP);
    lv_obj_align(SettingResolution->label_back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(SettingResolution->label_back, LV_PCT(100));
    lv_obj_add_style(SettingResolution->label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes title (标题)
    SettingResolution->title = lv_label_create(SettingResolution->cont_top);
    lv_label_set_text(SettingResolution->title, str_language_resolution[get_curr_language()]);
    lv_label_set_long_mode(SettingResolution->title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(SettingResolution->title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(SettingResolution->title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(SettingResolution->title, LV_ALIGN_CENTER, 0, 0);
    // 创建设置选项容器
    lv_obj_t *settings_cont = lv_obj_create(SettingResolution->resscr);
    lv_obj_set_size(settings_cont, 600, MENU_CONT_SIZE);
    lv_obj_align(settings_cont, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_flex_flow(settings_cont, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(settings_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(settings_cont, 10, 0);

    PARAM_MENU_S menu_param = {0};
    PARAM_GetMenuParam(&menu_param);

    // 创建分辨率选项按钮
    static lv_point_precise_t line_points_pool[PARAM_MENU_ITEM_MAX][2];
    MLOG_DBG("ItemCnt: %d\n", menu_param.PhotoSize.ItemCnt);
    for(uint32_t i = 0; i < menu_param.PhotoSize.ItemCnt; i++) {
        lv_obj_t *btn = lv_button_create(settings_cont);
        if(!btn) continue; // 如果按钮创建失败则跳过

        lv_obj_set_size(btn, 560, MENU_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, (MENU_BTN_SIZE + 10) * i);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 5, LV_PART_MAIN);
        // 添加事件处理器，传入容器对象作为用户数据
        lv_obj_add_event_cb(btn, screen_photoResolution_btn_event_handler, LV_EVENT_ALL, settings_cont);

        lv_obj_t *label = lv_label_create(btn);
        if(!label) continue; // 如果标签创建失败则跳过

        lv_label_set_text(label, menu_param.PhotoSize.Items[i].Desc);
        MLOG_DBG("Resolution: %s\n", menu_param.PhotoSize.Items[i].Desc);
        lv_obj_set_style_text_font(label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        // 设置标签位置和大小
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        if(i == res_Current_Index_s / 2) {
            {
                lv_obj_t *label1 = lv_label_create(btn);
                lv_obj_set_style_text_color(label1, lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(label1, "" LV_SYMBOL_OK " ");
                lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);
                lv_obj_align(label1, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }

        lv_obj_t *line = lv_line_create(settings_cont);
        int y_position = (MENU_BTN_SIZE + 10) * (i + 1) - 4; // 计算y坐标  //横线在下方,且第一个btn不用画线
        // 使用点数组池中的第i组
        line_points_pool[i][0].x = 10;
        line_points_pool[i][0].y = y_position;
        line_points_pool[i][1].x = 570;
        line_points_pool[i][1].y = y_position;
        lv_line_set_points(line, line_points_pool[i], 2);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0x5F5F5F), 0);
    }
    //先设置焦点控件,再进行滚动,否则会直接滚动到最下,不知什么原因.
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(settings_cont, res_Current_Index_s);
    lv_group_focus_obj(chlid);
    lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_y(settings_cont, ((res_Current_Index_s / 2) * MENU_BTN_SIZE), LV_ANIM_OFF);
    // //设置焦点渐变
    // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
    // //设置焦点BG
    // lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    //设置焦点标签颜色
    lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 在上方添加一条分割线
    lv_obj_t *up_line                       = lv_line_create(SettingResolution->resscr);
    static lv_point_precise_t points_line[] = {{10, 60}, {640, 60}};
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *target_obj = lv_obj_get_child(settings_cont, res_Current_Index_s);
    // 初始化焦点组
    init_focus_group(settings_cont, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS,
                     photoRes_click_callback, target_obj);
    // 设置当前页面的按键处理器
    set_current_page_handler(handle_grid_navigation);
    takephoto_register_menu_callback(photoRes_menu_callback);

    // Update current screen layout.
    lv_obj_update_layout(SettingResolution->resscr);

    // Init events for screen.
    events_init_screen_SettingResolution(ui);
}
