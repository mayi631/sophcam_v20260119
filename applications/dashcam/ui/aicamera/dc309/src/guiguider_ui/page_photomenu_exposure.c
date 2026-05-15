/*
 * Copyright 2025 NXP
 * NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
 * accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
 * activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
 * comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
 * terms, then you may not retain, install, activate or otherwise use the software.
 */
#define DEBUG
#include "config.h"
#include "custom.h"
#include "cvi_ae.h"
#include "gui_guider.h"
#include "indev.h"
#include "lvgl.h"
#include "page_all.h"
#include "style_common.h"
#include <stdio.h>

#define GRID_COLS 1
#define GRID_ROWS 7
#define GRID_MAX_OBJECTS GRID_ROWS * GRID_COLS
static lv_obj_t *focusable_objects[GRID_MAX_OBJECTS];

extern char g_button_labelExp[32];

static uint8_t exposure_Current_Index_s = 6;

uint8_t get_EV_Level(void)
{
    return exposure_Current_Index_s / 2;
}

static void exposure_Del_Complete_anim_cb(lv_anim_t *a)
{
    if(ui_Get_PreScreen() == g_ui.page_photoMenu_Setting.menuscr) {
        ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                                &g_ui.screen_SettingExposure_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                false, true);
    } else if(ui_Get_PreScreen() == obj_vedioMenu_s) {
        ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, &g_ui.screen_SettingExposure_del, vedioMenu_Setting,
                                LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
    }
}

static void exposure_win_Delete_anim(void)
{
    lv_anim_t Delete_anim; //动画渐隐句柄
    // 创建透明度动画
    lv_anim_init(&Delete_anim);
    lv_anim_set_values(&Delete_anim, 0, 1);

    lv_anim_set_time(&Delete_anim, 6);

    // lv_anim_set_exec_cb(&Delete_anim, AIanim_objSet_Opa);
    lv_anim_set_path_cb(&Delete_anim, lv_anim_path_ease_out);
    // 设置动画完成回调（销毁对象）
    lv_anim_set_completed_cb(&Delete_anim, exposure_Del_Complete_anim_cb);

    lv_anim_start(&Delete_anim);
}

static void screen_SettingExposure_btn_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            if(ui_Get_PreScreen() == g_ui.page_photoMenu_Setting.menuscr) {
                ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                                      &g_ui.screen_SettingExposure_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                      false, true);
            } else if(ui_Get_PreScreen() == obj_vedioMenu_s) {
                ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, &g_ui.screen_SettingExposure_del, vedioMenu_Setting,
                                      LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
            };
            break;
        }
        default: break;
    }
}

void events_init_screen_SettingExposure(lv_ui_t *ui)
{
    lv_obj_add_event_cb(ui->page_exposure.btn_back, screen_SettingExposure_btn_back_event_handler, LV_EVENT_CLICKED,
                        ui);
}

void photoExpose_SelectFocus_OK(lv_event_t *e, lv_obj_t *obj)
{
    lv_obj_t *btn_clicked = NULL;
    if(obj == NULL) {
        btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
    } else {
        btn_clicked = obj;
    }
    lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(parent, exposure_Current_Index_s);

    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if(i == exposure_Current_Index_s) {
            //先设置焦点控件,再进行滚动,否则会直接滚动到最下,不知什么原因.
            lv_group_focus_obj(chlid);
            lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
            //设置焦点渐变
            // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
            //设置焦点BG
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

static void screen_SettingExposure_btn_event_handler(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e);
    switch(code) {
        case LV_EVENT_CLICKED: {
            lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {

                if(lv_obj_get_child(parent, i) == btn_clicked) {
                    if (exposure_Current_Index_s == i) {
                        // 如果点击的是当前选中的按钮，则不做任何操作
                        break;
                    }
                    exposure_Current_Index_s = i;
                    photoExpose_SelectFocus_OK(e, NULL);
                    // 获取按钮标签文本
                    lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
                    if(label && lv_obj_check_type(label, &lv_label_class)) {
                        const char *txt = lv_label_get_text(label);
                        if(txt) strncpy(g_button_labelExp, txt, sizeof(g_button_labelExp));

                        MLOG_DBG("event: %s\n", txt);
                        VI_PIPE ViPipe = 0;
                        ISP_EXPOSURE_ATTR_S stExpAttr;
                        int32_t ret = CVI_ISP_GetExposureAttr(ViPipe, &stExpAttr);
                        if (ret != 0) {
                            MLOG_ERR("CVI_ISP_GetExposureAttr failed with error code %d\n", ret);
                        } else {
                            if(strcmp(txt, "EV-3") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 / 4;
                            } else if(strcmp(txt, "EV-2") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 / 3;
                            } else if(strcmp(txt, "EV-1") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 / 2;
                            } else if(strcmp(txt, "EV0") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024;
                            } else if(strcmp(txt, "EV+1") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 * 2;
                            } else if(strcmp(txt, "EV+2") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 * 3;
                            } else if(strcmp(txt, "EV+3") == 0) {
                                stExpAttr.stAuto.u16EVBias = 1024 * 4;
                            } else {
                                MLOG_ERR("unknown event level: %s\n", txt);
                            }

                            stExpAttr.enOpType = OP_TYPE_AUTO;
                            ret = CVI_ISP_SetExposureAttr(ViPipe, &stExpAttr);
                            if (ret != 0) {
                                MLOG_ERR("CVI_ISP_SetExposureAttr failed with error code %d\n", ret);
                            } else {
                                MLOG_DBG("CVI_ISP_SetExposureAttr success: u16EVBias=%d\n", stExpAttr.stAuto.u16EVBias);
                            }
                        }
                    }
                    lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                    lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000), LV_PART_MAIN);
                } else {
                    lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
                    lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
                }
            }
            exposure_win_Delete_anim();
            break;

        }
        default: break;
    }
}

static void photoExpose_click_callback(lv_obj_t *obj)
{
    MLOG_DBG("photoExpose_click_callback\n");
    lv_obj_t *parent      = lv_obj_get_parent(obj); //获取发生点击事件的父控件
    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {

        if(lv_obj_get_child(parent, i) == obj) {
            if (exposure_Current_Index_s == i) {
                // 如果点击的是当前选中的按钮，则不做任何操作
                break;
            }
            exposure_Current_Index_s = i;
            photoExpose_SelectFocus_OK(NULL, obj);
            // 获取按钮标签文本
            lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
            if(label && lv_obj_check_type(label, &lv_label_class)) {
                const char *txt = lv_label_get_text(label);
                if(txt) strncpy(g_button_labelExp, txt, sizeof(g_button_labelExp));

                MLOG_DBG("event: %s\n", txt);
                VI_PIPE ViPipe = 0;
                ISP_EXPOSURE_ATTR_S stExpAttr;
                int32_t ret = CVI_ISP_GetExposureAttr(ViPipe, &stExpAttr);
                if (ret != 0) {
                    MLOG_ERR("CVI_ISP_GetExposureAttr failed with error code %d\n", ret);
                } else {
                    if(strcmp(txt, "EV-2") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 / 4;
                    } else if(strcmp(txt, "EV-1") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 / 2;
                    } else if(strcmp(txt, "EV-3") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 / 8;
                    } else if(strcmp(txt, "EV0") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024;
                    } else if(strcmp(txt, "EV+1") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 * 2;
                    } else if(strcmp(txt, "EV+3") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 * 8;
                    } else if(strcmp(txt, "EV+2") == 0) {
                        stExpAttr.stAuto.u16EVBias = 1024 * 4;
                    } else {
                        MLOG_ERR("unknown event level: %s\n", txt);
                    }

                    ret = CVI_ISP_SetExposureAttr(ViPipe, &stExpAttr);
                    if (ret != 0) {
                        MLOG_ERR("CVI_ISP_SetExposureAttr failed with error code %d\n", ret);
                    } else {
                        MLOG_DBG("CVI_ISP_SetExposureAttr success: u16EVBias=%d\n", stExpAttr.stAuto.u16EVBias);
                    }
                }
            }
            lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000), LV_PART_MAIN);
        } else {
            lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        }
    }
    exposure_win_Delete_anim();
}

static void photoExpose_menu_callback(void)
{
    MLOG_DBG("photoExpose_menu_callback\n");
    if(ui_Get_PreScreen() == g_ui.page_photoMenu_Setting.menuscr) {
        ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                              &g_ui.screen_SettingExposure_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                              false, true);
    } else if(ui_Get_PreScreen() == obj_vedioMenu_s) {
        ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, &g_ui.screen_SettingExposure_del, vedioMenu_Setting,
                              LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
    };
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
                    if(ui_Get_PreScreen() == g_ui.page_photoMenu_Setting.menuscr) {
                        ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr,
                                              g_ui.screenPhotoMenuSetting_del, &g_ui.screen_SettingExposure_del,
                                              photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                    } else if(ui_Get_PreScreen() == obj_vedioMenu_s) {
                        ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, &g_ui.screen_SettingExposure_del,
                                              vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                    };
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}
void photoMenu_Exposure(lv_ui_t *ui)
{

    MLOG_DBG("loading page_exposure...\n");

    SettingExposure_t *SettingExposure = &ui->page_exposure;
    SettingExposure->expos_del         = true;

    // 创建主页面1 容器
    if(SettingExposure->expos_scr != NULL) {
        if(lv_obj_is_valid(SettingExposure->expos_scr)) {
            MLOG_DBG("page_exposure->scr 仍然有效，删除旧对象\n");
            lv_obj_del(SettingExposure->expos_scr);
        } else {
            MLOG_DBG("page_exposure->scr 已被自动销毁，仅重置指针\n");
        }
        SettingExposure->expos_scr = NULL;
    }

    // Write codes scr
    SettingExposure->expos_scr = lv_obj_create(NULL);
    lv_obj_set_size( SettingExposure->expos_scr , H_RES, V_RES);
    lv_obj_add_style( SettingExposure->expos_scr , &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(SettingExposure->expos_scr, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // Write codes cont_top (顶部栏)
    SettingExposure->cont_top = lv_obj_create(SettingExposure->expos_scr);
    lv_obj_set_pos(SettingExposure->cont_top, 0, 0);
    lv_obj_set_size(SettingExposure->cont_top, H_RES, 60);
    lv_obj_set_scrollbar_mode(SettingExposure->cont_top, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_style(SettingExposure->cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes btn_back (返回按钮)
    SettingExposure->btn_back = lv_button_create(SettingExposure->cont_top);
    lv_obj_set_pos(SettingExposure->btn_back, 4, 4);
    lv_obj_set_size(SettingExposure->btn_back, 60, 52);
    lv_obj_add_style(SettingExposure->btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    SettingExposure->label_back = lv_label_create(SettingExposure->btn_back);
    lv_label_set_text(SettingExposure->label_back, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(SettingExposure->label_back, LV_LABEL_LONG_WRAP);
    lv_obj_align(SettingExposure->label_back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(SettingExposure->label_back, LV_PCT(100));
    lv_obj_add_style(SettingExposure->label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write codes title (标题)
    SettingExposure->title = lv_label_create(SettingExposure->cont_top);
    lv_label_set_text(SettingExposure->title, str_language_exposure_settings[get_curr_language()]);
    lv_label_set_long_mode(SettingExposure->title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(SettingExposure->title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(SettingExposure->title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(SettingExposure->title, LV_ALIGN_CENTER, 0, 0);
    // 创建设置选项容器
    lv_obj_t *settings_cont = lv_obj_create(SettingExposure->expos_scr);
    if(!settings_cont) return; // 如果容器创建失败则返回

    lv_obj_set_size(settings_cont, 600, MENU_CONT_SIZE);
    lv_obj_align(settings_cont, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(settings_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_flex_flow(settings_cont, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(settings_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(settings_cont, 10, 0);

    // 创建曝光值按钮
    const char *btn_labels[] = {"EV+3", "EV+2", "EV+1", "EV0", "EV-1", "EV-2", "EV-3"};
    static lv_point_precise_t line_points_pool[sizeof(btn_labels) / sizeof(btn_labels[0])][2];
    for(int i = 0; i < 7; i++) {
        lv_obj_t *btn = lv_button_create(settings_cont);
        if(!btn) continue; // 如果按钮创建失败则跳过

        lv_obj_set_size(btn, 560, MENU_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, (MENU_BTN_SIZE + 10) * i);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 5, LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *label = lv_label_create(btn);
        if(!label) continue; // 如果标签创建失败则跳过

        lv_label_set_text(label, btn_labels[i]);
        lv_obj_set_style_text_font(label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        // 添加事件处理器，传入容器对象作为用户数据
        lv_obj_add_event_cb(btn, screen_SettingExposure_btn_event_handler, LV_EVENT_ALL, settings_cont);

        if(i == exposure_Current_Index_s / 2) {
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
    lv_group_focus_obj(lv_obj_get_child(settings_cont, exposure_Current_Index_s));
    lv_obj_add_state(lv_obj_get_child(settings_cont, exposure_Current_Index_s), LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_y(settings_cont, ((exposure_Current_Index_s / 2) * MENU_BTN_SIZE), LV_ANIM_OFF);
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(settings_cont, exposure_Current_Index_s);
    //设置焦点渐变
    // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
    //设置焦点BG
    // lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    //设置焦点标签颜色
    lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 在上方添加一条分割线
    lv_obj_t *up_line                       = lv_line_create(SettingExposure->expos_scr);
    static lv_point_precise_t points_line[] = {{10, 60}, {640, 60}};
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *target_obj = lv_obj_get_child(settings_cont, exposure_Current_Index_s);
    // 初始化焦点组
    init_focus_group(settings_cont, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS,
                     photoExpose_click_callback, target_obj);
    // 设置当前页面的按键处理器
    set_current_page_handler(handle_grid_navigation);
    takephoto_register_menu_callback(photoExpose_menu_callback);

    // Update current screen layout.
    lv_obj_update_layout(SettingExposure->expos_scr);

    // Init events for screen.
    events_init_screen_SettingExposure(ui);
}
