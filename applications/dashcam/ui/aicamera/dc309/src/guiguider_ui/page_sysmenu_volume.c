/*
 * Copyright 2025 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */
// #define DEBUG
#include "config.h"
#include "custom.h"
#include "gui_guider.h"
#include "indev.h"
#include "linux/input.h"
#include "lvgl.h"
#include "mapi_ao.h"
#include "page_all.h"
#include "style_common.h"
#include "ui_common.h"
#include <stdio.h>

#define GRID_COLS 1
#define GRID_ROWS 3
#define GRID_MAX_OBJECTS GRID_ROWS * GRID_COLS

#define MAX_VOLUME 32
#define HALF_MAX_VOLUME (MAX_VOLUME / 2)

static lv_obj_t *focusable_objects[GRID_MAX_OBJECTS];
// 音量全局变量
extern int volume_level;
static lv_obj_t *volume_value_label = NULL;
static lv_obj_t *scr_float_set = NULL;
lv_obj_t *settings_cont = NULL;
extern char g_sysbtn_labelVolume[32];

lv_obj_t *obj_sysMenu_Volume_s;

static uint8_t volume_Current_Index_s = 2;

void volume_set(void);

uint8_t getaction_audio_Index(void)
{
    return volume_Current_Index_s / 2;
}

void setaction_audio_Index(uint8_t index)
{
    volume_Current_Index_s = index * 2;
}

static void volume_Del_Complete_anim_cb(lv_anim_t *a)
{
    ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                            false, true);
}

static void volume_win_Delete_anim(void)
{
    lv_anim_t Delete_anim; //动画渐隐句柄
    // 创建透明度动画
    lv_anim_init(&Delete_anim);
    lv_anim_set_values(&Delete_anim, 0, 1);

    lv_anim_set_time(&Delete_anim, 6);

    // lv_anim_set_exec_cb(&Delete_anim, AIanim_objSet_Opa);
    lv_anim_set_path_cb(&Delete_anim, lv_anim_path_ease_out);
    // 设置动画完成回调（销毁对象）
    lv_anim_set_completed_cb(&Delete_anim, volume_Del_Complete_anim_cb);

    lv_anim_start(&Delete_anim);
}

static void sysMenu_Volume_btn_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                  false, true);
            break;
        }
        default: break;
    }
}

void syamenu_Volume_SelectFocus_OK(lv_event_t *e)
{

    lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
    lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(parent, volume_Current_Index_s);

    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if(i == volume_Current_Index_s) {
            //先设置焦点控件,再进行滚动,否则会直接滚动到最下,不知什么原因.
            lv_group_focus_obj(chlid);
            lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
            // //设置焦点渐变
            // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
            // //设置焦点BG
            // lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            //设置焦点标签颜色
            lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xF09F20),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
            // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN |
            // LV_STATE_DEFAULT);

        } else {
            // lv_obj_t *other_child = lv_obj_get_child(parent, i);
            // //设置焦点渐变
            // lv_set_obj_grad_style(other_child, LV_GRAD_DIR_VER, lv_color_hex(0), lv_color_hex(0));
            // //设置焦点BG
            // lv_obj_set_style_bg_color(other_child, lv_color_hex(0), LV_PART_MAIN | LV_STATE_DEFAULT);
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

static void sysMenu_Volume_Select_btn_event_handler(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e);
    switch(code) {
        case LV_EVENT_CLICKED: {
            lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {

                if(lv_obj_get_child(parent, i) == btn_clicked) {
                    if(i/2<2) {
                        volume_Current_Index_s = i;
                        syamenu_Volume_SelectFocus_OK(e);
                        // 获取按钮标签文本
                        lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
                        if(label && lv_obj_check_type(label, &lv_label_class)) {
                            const char *txt = lv_label_get_text(label);
                            if(txt) strncpy(g_sysbtn_labelVolume, txt, sizeof(g_sysbtn_labelVolume));
                        }
                        lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                        lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000),
                                                      LV_PART_MAIN);
                        MESSAGE_S Msg = {0};
                        Msg.topic     = EVENT_MODEMNG_SETTING;
                        Msg.arg1      = PARAM_MENU_ACTION_AUDIO;
                        Msg.arg2      = volume_Current_Index_s / 2;
                        MODEMNG_SendMessage(&Msg);
                        volume_win_Delete_anim();
                    } else {
                        volume_set();
                    }
                } else {
                    lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                    lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
                }
            }
            break;
        }
        default: break;
    }
}

static void sysmenu_volume_click_callback(lv_obj_t* obj)
{
    MLOG_DBG("sysmenu_volume_click_callback\n");
    lv_obj_t* parent = lv_obj_get_parent(obj); // 获取发生点击事件的父控件
    for (uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if (lv_obj_get_child(parent, i) == obj) {
            if (i / 2 < 2) {
                volume_Current_Index_s = i;
                // 获取按钮标签文本
                lv_obj_t* label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
                if (label && lv_obj_check_type(label, &lv_label_class)) {
                    const char* txt = lv_label_get_text(label);
                    if (txt)
                        strncpy(g_sysbtn_labelVolume, txt, sizeof(g_sysbtn_labelVolume));
                }
                lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000),
                    LV_PART_MAIN);
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_SETTING;
                Msg.arg1 = PARAM_MENU_ACTION_AUDIO;
                Msg.arg2 = volume_Current_Index_s / 2;
                MODEMNG_SendMessage(&Msg);
                volume_win_Delete_anim();
            } else {
                volume_set();
            }
        } else {
            lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        }
    }
}

static void sysmenu_volume_menu_callback(void)
{
    MLOG_DBG("sysmenu_volume_menu_callback\n");
    ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
        false, true);
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
                    ui_load_scr_animation(&g_ui, &obj_sysMenu_Setting_s, 1, NULL, sysMenu_Setting,
                                            LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

void sysMenu_Volume(lv_ui_t *ui)
{

    // 创建主页面1 容器
    if(obj_sysMenu_Volume_s != NULL) {
        if(lv_obj_is_valid(obj_sysMenu_Volume_s)) {
            MLOG_DBG("obj_sysMenu_Volume_s 仍然有效，删除旧对象\n");
            lv_obj_del(obj_sysMenu_Volume_s);
        } else {
            MLOG_DBG("obj_sysMenu_Volume_s 已被自动销毁，仅重置指针\n");
        }
        obj_sysMenu_Volume_s = NULL;
    }

    // Write codes resscr
    obj_sysMenu_Volume_s = lv_obj_create(NULL);
    lv_obj_set_size( obj_sysMenu_Volume_s , H_RES, V_RES);
    lv_obj_add_style( obj_sysMenu_Volume_s , &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj_sysMenu_Volume_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // Write codes cont_top (顶部栏)
    lv_obj_t *cont_top = lv_obj_create(obj_sysMenu_Volume_s);
    lv_obj_set_pos(cont_top, 0, 0);
    lv_obj_set_size(cont_top, 640, 60);
    lv_obj_set_scrollbar_mode(cont_top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes btn_back (返回按钮)
    lv_obj_t* btn_back = lv_button_create(cont_top);
    lv_obj_set_pos(btn_back, 4, 4);
    lv_obj_set_size(btn_back, 60, 52);
    lv_obj_add_style(btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(label_back, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(label_back, LV_PCT(100));
    lv_obj_add_style(label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(btn_back, sysMenu_Volume_btn_back_event_handler, LV_EVENT_CLICKED, NULL);

    // Write codes title (标题)
    lv_obj_t* title = lv_label_create(cont_top);
    lv_label_set_text(title, str_language_action_sound[get_curr_language()]);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // 创建设置选项容器
    settings_cont = lv_obj_create(obj_sysMenu_Volume_s);
    lv_obj_set_size(settings_cont, 600, MENU_CONT_SIZE);
    lv_obj_align(settings_cont, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_flex_flow(settings_cont, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(settings_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(settings_cont, 10, 0);

    // 创建分辨率选项按钮
    const char *btn_labels[] = {str_language_off[get_curr_language()], str_language_on[get_curr_language()],str_language_volume_settings[get_curr_language()]};
    static lv_point_precise_t line_points_pool[sizeof(btn_labels) / sizeof(btn_labels[0])][2];

    for(uint8_t i = 0; i < sizeof(btn_labels) / sizeof(btn_labels[0]); i++) {
        lv_obj_t *btn = lv_button_create(settings_cont);
        if(!btn) continue; // 如果按钮创建失败则跳过

        lv_obj_set_size(btn, 560, MENU_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, (MENU_BTN_SIZE + 10) * i);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 5, LV_PART_MAIN);

        lv_obj_t *label = lv_label_create(btn);
        if(!label) continue; // 如果标签创建失败则跳过

        lv_label_set_text(label, btn_labels[i]);
        lv_obj_set_style_text_font(label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        // 添加事件处理器，传入容器对象作为用户数据
        lv_obj_add_event_cb(btn, sysMenu_Volume_Select_btn_event_handler, LV_EVENT_ALL, settings_cont);

        if(i == volume_Current_Index_s / 2) {
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
    lv_obj_t *chlid = lv_obj_get_child(settings_cont, volume_Current_Index_s);
    lv_group_focus_obj(chlid);
    lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_y(settings_cont, ((volume_Current_Index_s / 2) * MENU_BTN_SIZE), LV_ANIM_OFF);
    // //设置焦点渐变
    // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
    // //设置焦点BG
    // lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    //设置焦点标签颜色
    lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 在上方添加一条分割线
    lv_obj_t *up_line                       = lv_line_create(obj_sysMenu_Volume_s);
    static lv_point_precise_t points_line[] = {{10, 60}, {640, 60}};
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *target_obj = lv_obj_get_child(settings_cont, volume_Current_Index_s);
    // 初始化焦点组
    init_focus_group(settings_cont, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS, sysmenu_volume_click_callback, target_obj);
    // 设置当前页面的按键处理器
    set_current_page_handler(handle_grid_navigation);
    takephoto_register_menu_callback(sysmenu_volume_menu_callback);

    // Update current screen layout.
    lv_obj_update_layout(obj_sysMenu_Volume_s);
}

// arc拖动事件回调
static void sysvolume_arc_event_cb(lv_event_t* e)
{
    lv_obj_t* arc = lv_event_get_target(e);
    int32_t value = lv_arc_get_value(arc);

    // 将0-32的区间映射到0-100的显示区间
    int32_t display_value = (value * 100) / (HALF_MAX_VOLUME);

    volume_level = value; // 保持实际的音量等级不变

    if (volume_value_label) {
        lv_label_set_text_fmt(volume_value_label, "%d", display_value);
    }
}

// 释放事件回调 - 发送消息保存配置
static void sysvolume_released_cb(lv_event_t* e)
{
    UNUSED(e);
    MESSAGE_S Msg = { 0 };

    /* 同步音量到全局配置结构体，并写入到配置文件中 */
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1 = PARAM_MENU_AO_VOLUME;
    Msg.arg2 = volume_level;
    MODEMNG_SendMessage(&Msg);

    MLOG_INFO("Set Ao volume to: %d\n", volume_level);
    MAPI_AO_SetVolume(MEDIA_GetCtx()->SysHandle.aohdl, volume_level);
}

// 返回按钮事件
static void sysvolume_back_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        /* 确保拍照页面存在 */
        if (!obj_sysMenu_Volume_s || !lv_obj_is_valid(obj_sysMenu_Volume_s)) {
            MLOG_WARN("Camera screen not valid, recreating...\n");
            sysMenu_Volume(NULL);
        } else {
            lv_obj_t* target_obj = lv_obj_get_child(settings_cont, 4);
            init_focus_group(settings_cont, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS, sysmenu_volume_click_callback, target_obj);
            set_current_page_handler(handle_grid_navigation);
            lv_group_focus_obj(target_obj);
            lv_obj_add_state(target_obj, LV_STATE_FOCUS_KEY);
            takephoto_register_menu_callback(sysmenu_volume_menu_callback);

            for (uint8_t i = 0; i < lv_obj_get_child_cnt(settings_cont); i++) {
                lv_obj_t* child = lv_obj_get_child(settings_cont, i);
                lv_obj_t* label1 = lv_obj_get_child(child, 0);
                if (label1 != NULL) {
                    lv_obj_set_style_text_color(label1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                lv_obj_t* label2 = lv_obj_get_child(child, 1);
                if (label2 != NULL) {
                    lv_obj_del(label2);
                }
            }

            lv_obj_t* label1 = lv_label_create(target_obj);
            lv_obj_set_style_text_color(label1, lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(label1, "" LV_SYMBOL_OK " ");
            lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);
            lv_obj_align(label1, LV_ALIGN_RIGHT_MID, 0, 0);

            lv_obj_set_style_text_color(lv_obj_get_child(target_obj, 0), lv_color_hex(0xF09F20),
                LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_scr_load(obj_sysMenu_Volume_s);
        lv_obj_update_layout(obj_sysMenu_Volume_s);

        if (scr_float_set != NULL) {
            lv_obj_del(scr_float_set);
            scr_float_set = NULL;
        }
    }
}

static void sysmenu_volume_setting_menu_callback(void)
{
    lv_obj_t *back_btn = lv_obj_get_child(scr_float_set, 0);
    lv_obj_send_event(back_btn, LV_EVENT_CLICKED, NULL);
}
static void volume_setting_menu_callback(int key_code, int key_value)
{
     if(key_code == KEY_MENU && key_value == 1) {
        sysmenu_volume_setting_menu_callback();
     }
}

void volume_set(void)
{
    scr_float_set = lv_obj_create(NULL);
    // 添加事件处理器，拦截手势事件
    lv_obj_set_size( scr_float_set , H_RES, V_RES);
    lv_obj_add_style( scr_float_set , &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);


    // 顶部返回按钮（黄色背景）
    lv_obj_t *btn_back = lv_btn_create(scr_float_set);
    lv_obj_set_pos(btn_back, 4, 4);
    lv_obj_set_size(btn_back, 60, 52);
    lv_obj_set_style_shadow_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, sysvolume_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_label_set_text(label_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_pad_all(label_back, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_back, &lv_font_SourceHanSerifSC_Regular_30,
            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_back, LV_ALIGN_CENTER, 0, 0);

    // 顶部居中标题str_language_volume[get_curr_language()]
    lv_obj_t *title = lv_label_create(scr_float_set);
    lv_label_set_text(title, str_language_volume[get_curr_language()]);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(title, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // 圆形音量进度条
    lv_obj_t *arc = lv_arc_create(scr_float_set);
    lv_obj_set_size(arc, 260, 260);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 20);
    lv_arc_set_range(arc, 0, HALF_MAX_VOLUME);
    MAPI_AO_GetVolume(MEDIA_GetCtx()->SysHandle.aohdl, &volume_level);
    lv_arc_set_value(arc, volume_level);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFD600), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(arc, 32, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    // lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arc, sysvolume_arc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(arc, sysvolume_released_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_set_style_pad_all(arc, 16, LV_PART_KNOB);

    // 中间音符图标
    lv_obj_t *label_icon = lv_label_create(scr_float_set);
    lv_label_set_text(label_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(label_icon, lv_color_hex(0xFFD600), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label_icon, &lv_font_montserratMedium_45, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(label_icon, arc, LV_ALIGN_CENTER, 0, 0);

    // 圆心音量数值label
    volume_value_label = lv_label_create(scr_float_set);
    // lv_label_set_text_fmt(volume_value_label, "%d", volume_value);
    lv_label_set_text_fmt(volume_value_label, "%d", (volume_level * 100) / (HALF_MAX_VOLUME));
    lv_obj_set_style_text_color(volume_value_label, lv_color_hex(0xFFD600), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(volume_value_label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align_to(volume_value_label, arc, LV_ALIGN_OUT_BOTTOM_MID, 0, -16);

    lv_scr_load(scr_float_set);
    set_current_page_handler(volume_setting_menu_callback);
}
