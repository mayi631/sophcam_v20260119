// #define DEBUG
#include "gui_guider.h"
#include "lvgl.h"
#include "style_common.h"
#include <stdio.h>
// #include "events_init.h"
#include "config.h"
#include "custom.h"
#include "cvi_isp.h"
#include "indev.h"
#include "page_all.h"

#define GRID_COLS 1
#define GRID_ROWS 3
#define GRID_MAX_OBJECTS GRID_COLS * GRID_ROWS
static lv_obj_t *focusable_objects[GRID_MAX_OBJECTS];

extern char g_vediobtn_labelSharpness[32];

lv_obj_t *obj_Vedio_Sharpness_s; //底层窗口

static uint8_t sharpness_Current_Index_s = 2;

uint8_t getSharness_index(void)
{
    return sharpness_Current_Index_s/2;
}

static void sharpness_Del_Complete_anim_cb(lv_anim_t *a)
{
    ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                            true);
}

static void sharpness_win_Delete_anim(void)
{
    lv_anim_t Delete_anim; //动画渐隐句柄
    // 创建透明度动画
    lv_anim_init(&Delete_anim);
    lv_anim_set_values(&Delete_anim, 0, 1);

    lv_anim_set_time(&Delete_anim, 6);

    // lv_anim_set_exec_cb(&Delete_anim, AIanim_objSet_Opa);
    lv_anim_set_path_cb(&Delete_anim, lv_anim_path_ease_out);
    // 设置动画完成回调（销毁对象）
    lv_anim_set_completed_cb(&Delete_anim, sharpness_Del_Complete_anim_cb);

    lv_anim_start(&Delete_anim);
}

static void vedioSharpness_btn_back_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                  false, true);
            break;
        }
        default: break;
    }
}

void syamenu_Sharpness_SelectFocus_OK(lv_event_t *e,lv_obj_t *obj)
{
    lv_obj_t *btn_clicked = NULL;
    if(obj == NULL) {
        btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
    } else {
        btn_clicked = obj;
    }
    lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
    //获取焦点控件
    lv_obj_t *chlid = lv_obj_get_child(parent, sharpness_Current_Index_s);

    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {
        if(i == sharpness_Current_Index_s) {
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

static void vedioSharpness_btn_event_handler(lv_event_t *e)
{

    lv_event_code_t code = lv_event_get_code(e);
    switch(code) {
        case LV_EVENT_CLICKED: {
            lv_obj_t *btn_clicked = lv_event_get_target(e);         //获取发生点击事件的控件
            lv_obj_t *parent      = lv_obj_get_parent(btn_clicked); //获取发生点击事件的父控件
            for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {

                if(lv_obj_get_child(parent, i) == btn_clicked) {
                    if (sharpness_Current_Index_s == i) {
                        // 如果点击的是当前选中的按钮，则不做任何操作
                        break;
                    }
                    sharpness_Current_Index_s = i;
                    syamenu_Sharpness_SelectFocus_OK(e, NULL);
                    // 获取按钮标签文本
                    lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
                    if(label && lv_obj_check_type(label, &lv_label_class)) {
                        const char *txt = lv_label_get_text(label);
                        if(txt) strncpy(g_vediobtn_labelSharpness, txt, sizeof(g_vediobtn_labelSharpness));


                        MLOG_DBG("event: %s\n", txt);
                        VI_PIPE ViPipe = 0;
                        int32_t ret = 0;

                        ISP_PRESHARPEN_EDGE_EXT_ATTR_S stEdgeExtAttr = {0};
                        ret = CVI_ISP_GetPreSharpenEdgeExtAttr(ViPipe, &stEdgeExtAttr);
                        if (ret != 0) {
                            MLOG_ERR("CVI_ISP_GetPreSharpenEdgeExtAttr failed with error code %d\n", ret);
                        }
                        if (ret == 0) {
                            if (strcmp(txt, str_language_sharp[get_curr_language()]) == 0) {
                               for (int32_t i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; i++) {
                                    stEdgeExtAttr.stAuto.E5aEnhance[i] = 64;
                                    stEdgeExtAttr.stAuto.E5bEnhance[i] = 64;
                                    stEdgeExtAttr.stAuto.E5cEnhance[i] = 64;
                                    stEdgeExtAttr.stAuto.E7Enhance[i] = 64;
                               }
                            } else if (strcmp(txt, str_language_normal[get_curr_language()]) == 0) {
                                for (int32_t i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; i++) {
                                    stEdgeExtAttr.stAuto.E5aEnhance[i] = 48;
                                    stEdgeExtAttr.stAuto.E5bEnhance[i] = 48;
                                    stEdgeExtAttr.stAuto.E5cEnhance[i] = 48;
                                    stEdgeExtAttr.stAuto.E7Enhance[i] = 48;
                               }
                            } else if (strcmp(txt, str_language_soft[get_curr_language()]) == 0) {
                                for (int32_t i = 0; i < ISP_AUTO_ISO_STRENGTH_NUM; i++) {
                                    stEdgeExtAttr.stAuto.E5aEnhance[i] = 16;
                                    stEdgeExtAttr.stAuto.E5bEnhance[i] = 16;
                                    stEdgeExtAttr.stAuto.E5cEnhance[i] = 16;
                                    stEdgeExtAttr.stAuto.E7Enhance[i] = 16;
                               }
                            } else {
                                MLOG_ERR("unknown sharpness level: %s\n", txt);
                            }
                            ret = CVI_ISP_SetPreSharpenEdgeExtAttr(ViPipe, &stEdgeExtAttr);
                            if (ret != 0) {
                                MLOG_ERR("CVI_ISP_SetPreSharpenEdgeExtAttr failed with error code %d\n", ret);
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
            sharpness_win_Delete_anim();

        }; break;
        default: {
            // 处理其他事件
            break;
        }
    }
}

static void vedioSharpness_click_callback(lv_obj_t *obj)
{
    MLOG_DBG("vedioSharpness_click_callback\n");
    lv_obj_t *parent      = lv_obj_get_parent(obj); //获取发生点击事件的父控件
    for(uint8_t i = 0; i < lv_obj_get_child_cnt(parent); i++) {

        if(lv_obj_get_child(parent, i) == obj) {
            if (sharpness_Current_Index_s == i) {
                // 如果点击的是当前选中的按钮，则不做任何操作
                break;
            }
            sharpness_Current_Index_s = i;
            syamenu_Sharpness_SelectFocus_OK(NULL, obj);
            // 获取按钮标签文本
            lv_obj_t *label = lv_obj_get_child(lv_obj_get_child(parent, i), 0);
            if(label && lv_obj_check_type(label, &lv_label_class)) {
                const char *txt = lv_label_get_text(label);
                if(txt) strncpy(g_vediobtn_labelSharpness, txt, sizeof(g_vediobtn_labelSharpness));


                MLOG_DBG("event: %s\n", txt);
                VI_PIPE ViPipe = 0;
                ISP_SHARPEN_ATTR_S stSharp;
                int32_t ret = CVI_ISP_GetSharpenAttr(ViPipe, &stSharp);
                if (ret == 0) {
                    if (strcmp(txt, str_language_sharp[get_curr_language()]) == 0) {
                    //
                    } else if (strcmp(txt, str_language_normal[get_curr_language()]) == 0) {
                        //
                    } else if (strcmp(txt, str_language_soft[get_curr_language()]) == 0) {
                        //
                    } else {
                        MLOG_ERR("unknown sharpness level: %s\n", txt);
                    }
                    ret = CVI_ISP_SetSharpenAttr(ViPipe, &stSharp);
                    if (ret != 0) {
                        MLOG_ERR("CVI_ISP_SetSharpenAttr failed with error code %d\n", ret);
                    }
                } else {
                    MLOG_ERR("CVI_ISP_GetSharpenAttr failed with error code %d\n", ret);
                }

            }
            lv_obj_add_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xFF0000), LV_PART_MAIN);
        } else {
            lv_obj_clear_state(lv_obj_get_child(parent, i), LV_STATE_PRESSED);
            lv_obj_set_style_border_color(lv_obj_get_child(parent, i), lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        }
    }
    sharpness_win_Delete_anim();
}

static void vedioSharpness_menu_callback(void)
{
    MLOG_DBG("vedioSharpness_menu_callback\n");
    ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
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
                    ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0,
                                          0, false, true);
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

void vedioMenu_Sharpness(lv_ui_t *ui)
{
    // 创建主页面1 容器
    if(obj_Vedio_Sharpness_s != NULL) {
        if(lv_obj_is_valid(obj_Vedio_Sharpness_s)) {
            MLOG_DBG("obj_Vedio_Sharpness_s 仍然有效，删除旧对象\n");
            lv_obj_del(obj_Vedio_Sharpness_s);
        } else {
            MLOG_DBG("obj_Vedio_Sharpness_s 已被自动销毁，仅重置指针\n");
        }
        obj_Vedio_Sharpness_s = NULL;
    }

    // 创建底层控件
    obj_Vedio_Sharpness_s = lv_obj_create(NULL);
    lv_obj_set_size(obj_Vedio_Sharpness_s, H_RES, V_RES);
    lv_obj_add_style(obj_Vedio_Sharpness_s, &style_common_main_bg, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj_Vedio_Sharpness_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // Write codes cont_top (顶部栏)
    lv_obj_t *cont_top = lv_obj_create(obj_Vedio_Sharpness_s);
    lv_obj_set_pos(cont_top, 0, 0);
    lv_obj_set_size(cont_top, H_RES, 60);
    lv_obj_set_scrollbar_mode(cont_top, LV_SCROLLBAR_MODE_OFF);

    // Write style for cont_top, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_add_style(cont_top, &style_common_cont_top, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t* btn_back = lv_button_create(cont_top);
    lv_obj_set_pos(btn_back, 4, 4);
    lv_obj_set_size(btn_back, 60, 52);
    lv_obj_add_style(btn_back, &style_common_btn_back, LV_PART_MAIN | LV_STATE_DEFAULT);
    // 为返回按钮添加事件处理
    lv_obj_add_event_cb(btn_back, vedioSharpness_btn_back_event_handler, LV_EVENT_CLICKED, ui);

    lv_obj_t *label_back = lv_label_create(btn_back);
    lv_obj_set_style_text_align(label_back, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(label_back, "" LV_SYMBOL_LEFT "");
    lv_label_set_long_mode(label_back, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_back, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(label_back, LV_PCT(100));
    lv_obj_add_style(label_back, &style_common_label_back, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t* title = lv_label_create(cont_top);
    lv_label_set_text(title, str_language_sharpness[get_curr_language()]);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 70, 50);
    lv_obj_set_style_pad_all(cont_top, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // Write codes cont_settings (设置选项容器)
    lv_obj_t *cont_settings = lv_obj_create(obj_Vedio_Sharpness_s);
    lv_obj_set_size(cont_settings, 600, MENU_CONT_SIZE);
    lv_obj_align(cont_settings, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(cont_settings, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont_settings, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_flex_flow(cont_settings, LV_FLEX_FLOW_COLUMN);
    // lv_obj_set_flex_align(cont_settings, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
    //                       LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont_settings, 10, 0);

    // 创建设置按钮
    const char *btn_labels[] = {str_language_sharp[get_curr_language()], str_language_normal[get_curr_language()], str_language_soft[get_curr_language()]};
    static lv_point_precise_t line_points_pool[sizeof(btn_labels) / sizeof(btn_labels[0])][2];
    for(int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_button_create(cont_settings);
        if(!btn) continue; // 如果按钮创建失败则跳过

        lv_obj_set_size(btn, 560, MENU_BTN_SIZE);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, (MENU_BTN_SIZE + 10) * i);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *label = lv_label_create(btn);
        if(!label) continue; // 如果标签创建失败则跳过

        lv_label_set_text(label, btn_labels[i]);
        // font_setting(label, FONT_SC16, (font_size_config_t){.mode = FONT_SIZE_MODE_CUSTOM, .size = FONT_SIZE_22},
        //              0x000000, 0, 0);
        lv_obj_set_style_text_font(label, get_usr_fonts(ALI_PUHUITI_FONTPATH, MENU_FONT_SIZE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

        // 添加事件处理器，传入容器对象作为用户数据
        lv_obj_add_event_cb(btn, vedioSharpness_btn_event_handler, LV_EVENT_ALL, cont_settings);

        if(i == sharpness_Current_Index_s / 2) {
            {
                lv_obj_t *label1 = lv_label_create(btn);
                lv_obj_set_style_text_color(label1, lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(label1, "" LV_SYMBOL_OK " ");
                lv_label_set_long_mode(label1, LV_LABEL_LONG_WRAP);
                lv_obj_align(label1, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }

        lv_obj_t *line = lv_line_create(cont_settings);
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
    lv_obj_t *chlid = lv_obj_get_child(cont_settings, sharpness_Current_Index_s);
    lv_group_focus_obj(chlid);
    lv_obj_add_state(chlid, LV_STATE_FOCUS_KEY);
    lv_obj_scroll_to_y(cont_settings, ((sharpness_Current_Index_s / 2) * MENU_BTN_SIZE), LV_ANIM_OFF);
    //设置焦点渐变
    // lv_set_obj_grad_style(chlid, LV_GRAD_DIR_VER, lv_color_hex(0xFBDEBD), lv_color_hex(0xF09F20));
    // //设置焦点BG
    // lv_obj_set_style_bg_color(chlid, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    //设置焦点标签颜色
    lv_obj_set_style_text_color(lv_obj_get_child(chlid, 0), lv_color_hex(0xF09F20), LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_text_color(lv_obj_get_child(chlid,1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

    // 在上方添加一条分割线
    lv_obj_t *up_line                       = lv_line_create(obj_Vedio_Sharpness_s);
    static lv_point_precise_t points_line[] = {{10, 60}, {640, 60}};
    lv_line_set_points(up_line, points_line, 2);
    lv_obj_set_style_line_width(up_line, 2, 0);
    lv_obj_set_style_line_color(up_line, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *target_obj = lv_obj_get_child(cont_settings, sharpness_Current_Index_s);
    // 初始化焦点组
    init_focus_group(cont_settings, GRID_COLS, GRID_ROWS, focusable_objects, GRID_MAX_OBJECTS,
                     vedioSharpness_click_callback, target_obj);
    // 设置当前页面的按键处理器
    set_current_page_handler(handle_grid_navigation);
    takephoto_register_menu_callback(vedioSharpness_menu_callback);
}
