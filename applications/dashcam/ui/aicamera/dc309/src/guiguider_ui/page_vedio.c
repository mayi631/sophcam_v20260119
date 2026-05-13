#define DEBUG
#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
// #include "events_init.h"
#include "config.h"
#include "custom.h"
#include "page_all.h"
#include <time.h>
#include "ui_common.h"
#include "common/takephoto.h"
#include "indev.h"
#include "icon_select_popup.h"
#include "page_vediomenu_res.h"
#include "page_sysmenu_brightness.h"
#include "infrared.h"
#include "anip_service.h"
#include "media_osd.h"
#include "animal_labels.h"
#include "mlog.h"

lv_obj_t *label_Vedio_Durtime_s; //录像时长
lv_obj_t *obj_vedio_s;           //底层窗口
lv_obj_t *dot_red_s;              //闪烁红点
lv_obj_t *video_red_level_s;         //red_light图标
lv_obj_t *img_sdonline_s = NULL;
lv_obj_t *img_batter_s = NULL;
lv_obj_t *label_available_video_s = NULL;//剩余录像时长
static lv_obj_t *img_effect_s = NULL;  //特效图标
static uint8_t g_flash_led_index = 0;

// lv_obj_t *label_datatime_s = NULL;
extern lv_style_t ttf_font_24;
extern lv_style_t ttf_font_16;
static lv_timer_t *date_timer_s = NULL; //时间更新定时器

extern const char *effect_style_small[];//特效图片数组

extern uint8_t is_start_video;     //录像状态
extern bool is_video_mode;

/*
 * 与 EVENT_MODEMNG_RECODER_STARTSTATU 对齐后用 lv_tick 算已录秒数，
 * 避免“定时器里先显示再自增”的相位误差。
 * 注意：不要用“显示秒数减常数”去贴 ffprobe，否则前几秒会一直卡在 00:00:00。
 */
static uint32_t s_vedio_rec_sync_tick;
static uint8_t s_vedio_rec_time_synced;

static uint32_t page_vedio_rec_elapsed_sec_for_display(void)
{
    if (!s_vedio_rec_time_synced)
        return 0;
    return lv_tick_elaps(s_vedio_rec_sync_tick) / 1000;
}
extern char *batter_image_big[];
extern char *red_light_image_level[];
extern int32_t g_batter_image_index;

static lv_timer_t *g_zoom_longpress_timer = NULL;  // 长按定时器
static int g_zoom_longpress_dir = 0;               // 长按方向: 0=无, 1=缩小, 2=放大
static bool g_zoom_longpress_active = false;       // 是否正在长按

lv_obj_t *g_video_top_controls[6];  // 存储视频页面顶部控件对象

// ========== 实时动物检测相关 ==========
#define VIDEO_MAX_ANIMAL_BOXES 5

typedef struct {
    lv_obj_t *label;
    bool valid;
} video_animal_box_t;

static ANIP_SERVICE_HANDLE_T g_video_anip_handle = -1;
static lv_obj_t *g_video_ani_canvas = NULL;
static bool g_video_anip_enabled = false;
static video_animal_box_t g_video_ani_boxes[VIDEO_MAX_ANIMAL_BOXES] = {0};
#define VIDEO_ANI_LABEL_BG_COLOR lv_color_hex(0x8000FF00)
// ===================================

static void video_zoomin_key_cb(void);//w按键回调
static void video_zoomout_key_cb(void);//t按键回调
// left按键处理回调函数
static void video_left_callback(void);
// right按键处理回调函数
static void video_right_callback(void);

static void photo_zoom_event_cb(lv_event_t* e);

// 前向声明
extern void update_video_top_controls_layout(void);
extern void brightness_set_level(int level);

// 检查是否在视频页面
static bool is_in_video_page(void)
{
    return (obj_vedio_s != NULL &&
            lv_obj_is_valid(obj_vedio_s) &&
            lv_screen_active() == obj_vedio_s);
}

void page_vedio_on_recorder_started(int32_t rec_id)
{
    if (rec_id != 0)
        return;
    if (is_start_video != VEDIO_START)
        return;
    /* 仅本段录像第一次 START 时清零；循环录像新文件若再报 START 不能重置 OSD */
    if (s_vedio_rec_time_synced)
        return;
    s_vedio_rec_time_synced = 1;
    s_vedio_rec_sync_tick = lv_tick_get();
    /* 1s 周期定时器可能与 START 错开半周期，立刻跑一次刷新 */
    if (date_timer_s != NULL)
        lv_timer_ready(date_timer_s);
}

void page_vedio_on_recorder_stopped(int32_t rec_id)
{
    if (rec_id != 0)
        return;
    s_vedio_rec_time_synced = 0;
}

// 视频分辨率选择回调
static void icon_select_video_res_callback(uint32_t index, void *user_data)
{
    MLOG_DBG("视频分辨率选择: index=%d\n", index);

    // 设置新的分辨率索引
    video_setRes_Index(index);

    // 更新UI显示
    if (g_video_top_controls[1] && lv_obj_is_valid(g_video_top_controls[1])) {
        const char *icon = video_getRes_Icon();
        if (icon != NULL) {
            show_image(g_video_top_controls[1], icon);
        }
    }

    // 发送消息更新分辨率参数
    MESSAGE_S Msg = {0};
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1 = PARAM_MENU_VIDEO_SIZE;
    Msg.arg2 = index;
    MODEMNG_SendMessage(&Msg);
}

// 分辨率按钮点击回调
static void video_res_btn_click_cb(lv_event_t *e)
{
    if (!is_in_video_page()) {
        MLOG_DBG("不在视频页面，忽略点击\n");
        return;
    }

    // 如果弹窗已存在且是同一类型，则关闭
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }

    // 构建分辨率选项数组
    PARAM_MENU_S menu_param = {0};
    PARAM_GetMenuParam(&menu_param);

    static icon_select_item_t video_res_items[6];
    static char video_res_labels[6][16];
    uint8_t item_count = menu_param.VideoSize.ItemCnt;
    if (item_count > 6) item_count = 6;

    for (uint8_t i = 0; i < item_count; i++) {
        video_res_items[i].icon = video_getRes_IconByIndex(i);
        snprintf(video_res_labels[i], sizeof(video_res_labels[i]), "%s",
                 menu_param.VideoSize.Items[i].Desc);
        video_res_items[i].label = video_res_labels[i];
    }

    // 创建弹窗
    create_icon_select_popup(obj_vedio_s, ICON_SELECT_VIDEO_RESOLUTION,
                              video_res_items, item_count,
                              video_getRes_Index(),
                              icon_select_video_res_callback, NULL);
}

// 红外灯亮度选择回调
static void icon_select_video_redlight_callback(uint32_t index, void *user_data)
{
    MLOG_DBG("视频红外灯亮度选择: index=%d\n", index);

    // 如果要开启红外灯（亮度>0），检查电量
    if (index > 0) {
        // 检查是否空格电量（电池图标索引为1表示低电量0%-25%）
        if (g_batter_image_index == 1) {
            MLOG_ERR("电量过低，无法开启红外灯\n");
            // 可以在这里添加提示用户电量低的逻辑
            return;
        }
    }

    // 设置亮度级别 (0-7)
    brightness_level = index;

    // 实际设置红外灯亮度
    if (index == 0) {
        led_off();
        ircut_off();
    } else {
        // 根据电池电量限制最大亮度档位
        int8_t max_level = get_max_red_light_level();
        if (brightness_level > max_level) {
            brightness_level = max_level;
            MLOG_DBG("电池电量限制，红外灯亮度自动降档至 %d\n", brightness_level);
        }
        led_on_with_brightness(brightness_level);
    }

    // 更新UI显示
    if (g_video_top_controls[2] && lv_obj_is_valid(g_video_top_controls[2])) {
        if (brightness_level > 6) {
            show_image(g_video_top_controls[2], red_light_image_level[6]);
        } else if (brightness_level > 0) {
            show_image(g_video_top_controls[2], red_light_image_level[brightness_level - 1]);
        }
    }

    // 更新布局
    update_video_top_controls_layout();
}

// 红外灯亮度按钮点击回调
static void video_redlight_btn_click_cb(lv_event_t *e)
{
    if (!is_in_video_page()) {
        MLOG_DBG("不在视频页面，忽略点击\n");
        return;
    }

    // 如果弹窗已存在且是同一类型，则关闭
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }

    // 根据电池电量获取最大允许的档位
    int8_t max_level = get_max_red_light_level();

    // 构建红外灯亮度选项数组（0=关闭，1-max_level=亮度档位）
    // 选项数量 = 1（关闭选项）+ max_level（允许的最大档位）
    int item_count = 1 + max_level;
    if (item_count > 8) item_count = 8;  // 最多8个选项

    static icon_select_item_t redlight_items[8];
    static char redlight_labels[8][16];

    // 第一个选项是关闭
    redlight_items[0].icon = "guanbi.png";
    snprintf(redlight_labels[0], sizeof(redlight_labels[0]), "关闭");
    redlight_items[0].label = redlight_labels[0];

    // 后续选项是亮度档位
    for (int i = 1; i < item_count; i++) {
        int level_index = i - 1;  // 对应 red_light_image_level 数组索引
        if (level_index < 7) {
            redlight_items[i].icon = red_light_image_level[level_index];
        } else {
            redlight_items[i].icon = red_light_image_level[6];
        }
        snprintf(redlight_labels[i], sizeof(redlight_labels[i]), "等级%d", i);
        redlight_items[i].label = redlight_labels[i];
    }

    // 计算当前选中的索引（根据当前亮度级别和最大档位限制）
    uint32_t selected_index = 0;
    if (brightness_level > 0) {
        if (brightness_level > max_level) {
            selected_index = max_level;  // 如果当前档位超出限制，选中最大档位
        } else {
            selected_index = brightness_level;
        }
    }

    // 创建弹窗
    create_icon_select_popup(obj_vedio_s, ICON_SELECT_REDLIGHT,
                              redlight_items, item_count,
                              selected_index,
                              icon_select_video_redlight_callback, NULL);

    MLOG_DBG("录像模式红外灯弹窗：最大档位=%d，选项数量=%d，当前选中=%d\n",
             max_level, item_count, selected_index);
}

// 屏幕亮度选择回调
static void icon_select_video_brightness_callback(uint32_t index, void *user_data)
{
    MLOG_DBG("视频屏幕亮度选择: index=%d\n", index);

    // 设置屏幕亮度 (0-6)
    setsysMenu_brightness_Index(index);
    brightness_set_level(index + 1);  // level 是 1-7

    // 更新UI显示
    if (g_video_top_controls[4] && lv_obj_is_valid(g_video_top_controls[4])) {
        char* brightness_buf[] = { "1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png" };
        show_image(g_video_top_controls[4], brightness_buf[get_curr_brightness()]);
    }

    // 发送消息更新亮度参数
    MESSAGE_S Msg = {0};
    Msg.topic = EVENT_MODEMNG_SETTING;
    Msg.arg1 = PARAM_MENU_BRIGHTNESS;
    Msg.arg2 = index;
    MODEMNG_SendMessage(&Msg);
}

// 屏幕亮度按钮点击回调
static void video_brightness_btn_click_cb(lv_event_t *e)
{
    if (!is_in_video_page()) {
        MLOG_DBG("不在视频页面，忽略点击\n");
        return;
    }

    // 如果弹窗已存在且是同一类型，则关闭
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }

    // 构建屏幕亮度选项数组
    static icon_select_item_t brightness_items[7];
    static char brightness_labels[7][8];
    char* brightness_buf[] = { "1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png" };

    for (uint8_t i = 0; i < 7; i++) {
        brightness_items[i].icon = brightness_buf[i];
        snprintf(brightness_labels[i], sizeof(brightness_labels[i]), "等级%d", i + 1);
        brightness_items[i].label = brightness_labels[i];
    }

    // 创建弹窗
    create_icon_select_popup(obj_vedio_s, ICON_SELECT_BRIGHTNESS,
                              brightness_items, 7,
                              get_curr_brightness(),
                              icon_select_video_brightness_callback, NULL);
}

// 清理视频页面资源的通用函数
static void cleanup_vedio_page_resources(void)
{
    takephoto_unregister_all_callback();//取消所有按键回调
    if(is_start_video == VEDIO_START) {
        is_start_video = VEDIO_STOP;
        MESSAGE_S Msg  = {0};
        Msg.topic      = EVENT_MODEMNG_STOP_REC;
        MODEMNG_SendMessage(&Msg);
        enable_touch_events(); // 停止录像，恢复TP事件
    }

    // 清理定时器和动画，避免在页面切换时访问已销毁的对象
    if(date_timer_s != NULL)
    {
        lv_timer_delete(date_timer_s);
        date_timer_s = NULL;
    }
    //删除关于特效的资源
    delete_all_handle();
    delete_viewfinder();//销毁取景框
    delete_zoom_bar();//销毁zoombar
    // 释放缩放相关资源
    delete_zoombar_timer_handler();
    // 删除图标选择弹窗
    delete_icon_select_popup();

    // 停止实时动物检测
    if (g_video_anip_enabled) {
        g_video_anip_enabled = false;
        if (g_video_anip_handle >= 0) {
            ANIP_SERVICE_Clear_Rects(g_video_anip_handle);
            ANIP_SERVICE_Destroy(g_video_anip_handle);
            g_video_anip_handle = -1;
        }
        ANIP_SERVICE_Unregister_DrawRects_Callback();
        ANIP_SERVICE_Unregister_Result_Callback();
        MLOG_INFO("[VANIP] Animal detection stopped on video page\n");
    }
    /* 销毁覆盖层 */
    if (g_video_ani_canvas != NULL) {
        if (lv_obj_is_valid(g_video_ani_canvas)) {
            lv_obj_del(g_video_ani_canvas);
        }
        g_video_ani_canvas = NULL;
    }
    for (int i = 0; i < VIDEO_MAX_ANIMAL_BOXES; i++) {
        if (g_video_ani_boxes[i].label != NULL) {
            if (lv_obj_is_valid(g_video_ani_boxes[i].label)) {
                lv_obj_del(g_video_ani_boxes[i].label);
            }
            g_video_ani_boxes[i].label = NULL;
        }
        g_video_ani_boxes[i].valid = false;
    }
}

//参数动态更新回调
static void video_var_dynamic_update(lv_timer_t *timer)
{
    // lv_ui_t *ui  = (lv_ui_t *)lv_timer_get_user_data(timer);
    time_t now   = time(NULL);
    struct tm *t = localtime(&now);
    // MLOG_DBG("%s[%d]   %d...\n",__func__,__LINE__,lv_obj_is_valid(obj_vedio_s));
    {
        // 检查对象有效性，避免在页面切换时访问已销毁的对象
        if (!lv_obj_is_valid(obj_vedio_s)) {
            return;
        }

        // // 更新日期
        // lv_label_set_text_fmt(label_datatime_s, "%04d/%02d/%02d", t->tm_year + 1900, t->tm_mon + 1,
        //                       t->tm_mday);
        if(is_start_video == VEDIO_STOP)
        {
            if(getSelect_Index() == TIME_FLAG_OFF && !lv_obj_has_flag(label_Vedio_Durtime_s,LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_add_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
            }
            // 更新时间
            lv_label_set_text_fmt(label_Vedio_Durtime_s, "%04d/%02d/%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        //电池等级更新
        show_image(img_batter_s, batter_image_big[g_batter_image_index]);
        lv_label_set_text_fmt(label_available_video_s, "%s", video_Calculateremainingvideo());
        if(ui_common_cardstatus()) {
            show_image(img_sdonline_s, "icon_card_online.png");
        } else {
            show_image(img_sdonline_s, "icon_card_offline.png");
        }
        //录像状态
        switch(is_start_video)
        {
            case VEDIO_STOP:
            {
                if(!lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
            }  ;break;
            case VEDIO_START:
            {
                if(!lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
                else if(lv_obj_has_flag(dot_red_s,LV_OBJ_FLAG_HIDDEN)&&lv_obj_is_valid(obj_vedio_s))
                {
                    lv_obj_remove_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
                }
                if (!s_vedio_rec_time_synced) {
                    lv_label_set_text_fmt(label_Vedio_Durtime_s, "%02d:%02d:%02d", 0, 0, 0);
                    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFF0000),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                    break;
                }
                {
                    uint32_t es = page_vedio_rec_elapsed_sec_for_display();
                    uint8_t hour = (uint8_t)(es / 3600);
                    uint8_t min = (uint8_t)((es % 3600) / 60);
                    uint8_t sec = (uint8_t)(es % 60);
                    lv_label_set_text_fmt(label_Vedio_Durtime_s, "%02d:%02d:%02d", hour, min, sec);
                    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFF0000),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }

            };break;
        }
    }
}

// 视频页面顶部控件布局更新
void update_video_top_controls_layout(void)
{
    int x_pos = 6;  // 起始X坐标

    // 1. 录像模式按钮 (74x47)
    if (g_video_top_controls[0] && lv_obj_is_valid(g_video_top_controls[0])) {
        lv_obj_set_pos(g_video_top_controls[0], x_pos, 0);
    }
    x_pos += 76 + 10;

    // 2. 分辨率按钮 (38x32)
    if (g_video_top_controls[1] && lv_obj_is_valid(g_video_top_controls[1])) {
        lv_obj_set_pos(g_video_top_controls[1], x_pos, 4);
    }
    x_pos += 40 + 10;

    // 3. 红光亮级按钮 (38x32)
    if (g_video_top_controls[2] && lv_obj_is_valid(g_video_top_controls[2])) {
        if (brightness_level > 0) {
            lv_obj_clear_flag(g_video_top_controls[2], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(g_video_top_controls[2], x_pos, 4);
            x_pos += 40 + 10;
        } else {
            lv_obj_add_flag(g_video_top_controls[2], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // // 4. ISO级别按钮
    // if (g_video_top_controls[3] && lv_obj_is_valid(g_video_top_controls[3])) {
    //     lv_obj_set_pos(g_video_top_controls[3], x_pos, 4);
    // }
    // x_pos += 40 + 10;

    // 5. 屏幕亮度按钮
    if (g_video_top_controls[4] && lv_obj_is_valid(g_video_top_controls[4])) {
        lv_obj_set_pos(g_video_top_controls[4], x_pos, 4);
    }

    // x_pos += 40 + 10;
    // //EV值
    // if (g_video_top_controls[5] && lv_obj_is_valid(g_video_top_controls[5])) {
    //     lv_obj_set_pos(g_video_top_controls[5], x_pos, 14);
    // }

    MLOG_DBG("视频页面顶部控件布局已更新，brightness_level=%d\n", brightness_level);
}


//渐隐动画完成回调
void animCompleted_objDel_cb(lv_anim_t *a)
{
    //移除标志
    if(getSelect_Index() == TIME_FLAG_ON) {
        lv_obj_remove_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_resume(date_timer_s);
    delete_all_handle();
}

// ok按键处理回调函数
static void video_sesor_switch_completed_callback(void)
{
    enable_touch_events(); // 恢复触摸
    enable_hardware_input_device(0);
    enable_hardware_input_device(1);
}

static void buttonVedio_All_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int Click_index = (int)lv_event_get_user_data(e);
    MLOG_DBG("code:%d, Click_index:%d\n", code, Click_index);

    switch(code) {
        case LV_EVENT_CLICKED: {
            // 清理视频页面资源
            cleanup_vedio_page_resources();
            if(Click_index == 1) {
                ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                      false, true);
            } else if(Click_index == 2) {
                // 进入拍照模式
                MESSAGE_S Msg = {0};
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PHOTO;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                // 使能对焦
                enable_focus();
                is_video_mode = false;
                ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                      false, true);
            } else if(Click_index == 3) {
                MESSAGE_S Msg = {0};
                takephoto_cancel_focus();
                // 通知mode关闭时要关闭sensor
                Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
                Msg.arg1      = 1;
                MODEMNG_SendMessage(&Msg);
                memset(&Msg, 0, sizeof(MESSAGE_S));
                // 进入BOOT模式
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1  = WORK_MODE_BOOT;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                is_video_mode = false;
                reset_effect();
                ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                                      true);
            } else if(Click_index == 4) {
                // 进入录像模式
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PLAYBACK;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                is_video_mode = false;
                ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
            }
            break;
        }
        default: break;
    }
}

void video_effect_scr_delete(void)
{
    delete_all_handle();
    lv_timer_ready(date_timer_s);
    lv_timer_resume(date_timer_s);
}

//特效选择回调
static void vedioEffect_Select_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED:
        {
            if(get_is_effect_exist() == true) {
                delete_all_handle();
                lv_timer_resume(date_timer_s);
            } else {
                // 添加隐藏时间标志
                lv_timer_pause(date_timer_s);
                // 创建滚动列表
                float_effect_creat(img_effect_s,obj_vedio_s);
                // 创建控件并启动渐渐隐藏动画
                create_gradually_hide_anim(animCompleted_objDel_cb,8000);
            }
            // Update current screen layout.
            lv_obj_update_layout(obj_vedio_s);
        } break;
        default: break;
    }
}

// 菜单按键处理回调函数
static void key_takephoto_menu_callback(void)
{
    MLOG_DBG("进入录像模式设置页面\n");

    // 清理视频页面资源
    cleanup_vedio_page_resources();

    ui_load_scr_animation(&g_ui, &obj_vedioMenu_s, 1, NULL, vedioMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0,
        false, true);
}

// UP按键处理回调函数
static void video_up_callback(void)
{
    g_flash_led_index = !g_flash_led_index;
    MESSAGE_S Msg = {0};
    Msg.topic     = EVENT_MODEMNG_SETTING;
    Msg.arg1      = PARAM_MENU_FLASH_LED;
    Msg.arg2      = g_flash_led_index;
    MODEMNG_SendMessage(&Msg);

}

// 模式切换按键处理回调函数
static void key_takephoto_mode_callback(void)
{
    MLOG_DBG("模式切换，进入拍照模式\n");

    // 清理视频页面资源
    cleanup_vedio_page_resources();

    MESSAGE_S Msg = {0};
    Msg.topic = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1 = WORK_MODE_PHOTO;
    MODEMNG_SendMessage(&Msg);
    // 复位缩放
    set_zoom_level(1);
    // 使能对焦
    enable_focus();
    is_video_mode = false;
    ui_load_scr_animation(&g_ui, &g_ui.page_photo.photoscr, g_ui.screenHomePhoto_del, NULL, Home_Photo, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                            false, true);
}

static void vieo_key_down_callback(void)
{
    create_simple_delete_dialog(NULL);//创建确认浮窗
}

// 长按菜单按键处理回调函数
static void video_long_menu_callback(void)
{
    cleanup_vedio_page_resources();
    MESSAGE_S Msg = {0};
    takephoto_cancel_focus();
    // 通知mode关闭时要关闭sensor
    Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
    Msg.arg1      = 1;
    MODEMNG_SendMessage(&Msg);
    memset(&Msg, 0, sizeof(MESSAGE_S));
    // 进入BOOT模式
    Msg.topic     = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1      = WORK_MODE_BOOT;
    MODEMNG_SendMessage(&Msg);
    // 复位缩放
    set_zoom_level(1);
    is_video_mode = false;
    reset_effect();
    ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

// ok按键处理回调函数
static void key_takephoto_ok_callback(void)
{
    if(get_is_effect_exist() == false) {
    } else {
        set_effect_ok();
    }
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
                    MESSAGE_S Msg = {0};
                    takephoto_cancel_focus();
                    // 通知mode关闭时要关闭sensor
                    Msg.topic     = EVENT_MODEMNG_SENSOR_STATE;
                    Msg.arg1      = 1;
                    MODEMNG_SendMessage(&Msg);
                    memset(&Msg, 0, sizeof(MESSAGE_S));
                    // 进入BOOT模式
                    Msg.topic     = EVENT_MODEMNG_MODESWITCH;
                    Msg.arg1      = WORK_MODE_BOOT;
                    MODEMNG_SendMessage(&Msg);
                    // 复位缩放
                    set_zoom_level(1);
                    is_video_mode = false;
                    reset_effect();
                    ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                          false, true);
                    cleanup_vedio_page_resources();
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}


static void video_redlight_callback(void)
{
    // 调用通用的红光UI更新函数
    extern void update_redlight_ui(void);
    update_redlight_ui();
    MLOG_DBG("视频红光亮级更新: brightness_level=%d\n", brightness_level);
}


static void key_takephoto_power_callback(void)
{
    MLOG_DBG("key_takephoto_power_callback\n");
    static bool power_key_count = false;
    power_key_count = !power_key_count;
    switch (power_key_count) {
    case false:
        restore_all_widgets();
        break;
    case true:
        hide_all_widgets(obj_vedio_s);
        break;
    }
}

// ========== 视频模式实时动物检测 ==========

/* 前向声明 */
static void video_anip_result_ui_update(void *user_data);

typedef struct {
    CVI_U32 count;
    ANIP_RESULT_S results[VIDEO_MAX_ANIMAL_BOXES];
} video_anip_result_data_t;

/* OSD层绘制框回调 */
static int video_anip_draw_rects_callback(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects)
{
    CVI_S32 ret = MEDIA_DrawRects(osd_id, num, rects);
    if (ret != 0) {
        MLOG_ERR("[VANIP] MEDIA_DrawRects failed: %d\n", ret);
    }
    return ret;
}

/* 动物识别结果回调 */
static void video_anip_result_callback(CVI_U32 osd_id, ANIP_RESULT_S* results, CVI_U32 count)
{
    (void)osd_id;
    if (!g_video_anip_enabled || count == 0 || results == NULL) {
        return;
    }
    static video_anip_result_data_t result_data;
    result_data.count = (count > VIDEO_MAX_ANIMAL_BOXES) ? VIDEO_MAX_ANIMAL_BOXES : count;
    for (CVI_U32 i = 0; i < result_data.count; i++) {
        result_data.results[i] = results[i];
    }
    lv_async_call(video_anip_result_ui_update, &result_data);
}

/* 在主线程中更新标签UI */
static void video_anip_result_ui_update(void *user_data)
{
    video_anip_result_data_t *result_data = (video_anip_result_data_t *)user_data;
    CVI_U32 count = result_data->count;

    if (g_video_ani_canvas == NULL || !lv_obj_is_valid(g_video_ani_canvas)) {
        return;
    }

    RECT_S rects[VIDEO_MAX_ANIMAL_BOXES] = {0};
    CVI_U32 rect_count = ANIP_SERVICE_Get_Rects(rects, VIDEO_MAX_ANIMAL_BOXES);

    /* 隐藏所有旧标签 */
    for (int i = 0; i < VIDEO_MAX_ANIMAL_BOXES; i++) {
        if (g_video_ani_boxes[i].label != NULL && lv_obj_is_valid(g_video_ani_boxes[i].label)) {
            lv_obj_add_flag(g_video_ani_boxes[i].label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    CVI_U32 show_count = (count < rect_count) ? count : rect_count;
    if (show_count > VIDEO_MAX_ANIMAL_BOXES) show_count = VIDEO_MAX_ANIMAL_BOXES;

    for (CVI_U32 i = 0; i < show_count; i++) {
        if (strlen(result_data->results[i].name) == 0 || result_data->results[i].cls_idx != 0) {
            continue;
        }

        RECT_S* rect = &rects[i];
        if (rect->u32Width <= 0 || rect->u32Height <= 0) {
            continue;
        }

        video_animal_box_t* box = &g_video_ani_boxes[i];

        if (box->label == NULL || !lv_obj_is_valid(box->label)) {
            box->label = lv_label_create(g_video_ani_canvas);
            if (box->label == NULL) continue;
            lv_obj_set_style_bg_color(box->label, VIDEO_ANI_LABEL_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(box->label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(box->label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(box->label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 16), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(box->label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(box->label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s", result_data->results[i].name);
        lv_label_set_text(box->label, label_text);

        CVI_S32 label_x = rect->s32X + 4;
        CVI_S32 label_y = rect->s32Y - 24;
        if (label_y < 0) label_y = rect->s32Y + 4;

        lv_obj_set_pos(box->label, label_x, label_y);
        lv_obj_clear_flag(box->label, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 创建覆盖层 */
static void video_create_anip_overlay(lv_obj_t *parent)
{
    if (parent == NULL || !lv_obj_is_valid(parent)) {
        MLOG_ERR("[VANIP] Parent invalid\n");
        return;
    }
    /* 先销毁已有的 */
    if (g_video_ani_canvas != NULL) {
        if (lv_obj_is_valid(g_video_ani_canvas)) {
            lv_obj_del(g_video_ani_canvas);
        }
        g_video_ani_canvas = NULL;
    }
    for (int i = 0; i < VIDEO_MAX_ANIMAL_BOXES; i++) {
        if (g_video_ani_boxes[i].label != NULL) {
            if (lv_obj_is_valid(g_video_ani_boxes[i].label)) {
                lv_obj_del(g_video_ani_boxes[i].label);
            }
            g_video_ani_boxes[i].label = NULL;
        }
        g_video_ani_boxes[i].valid = false;
    }

    g_video_ani_canvas = lv_obj_create(parent);
    if (g_video_ani_canvas == NULL) {
        MLOG_ERR("[VANIP] Failed to create overlay\n");
        return;
    }
    lv_obj_set_size(g_video_ani_canvas, H_RES, V_RES);
    lv_obj_set_pos(g_video_ani_canvas, 0, 0);
    lv_obj_set_style_bg_opa(g_video_ani_canvas, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(g_video_ani_canvas, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g_video_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(g_video_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_video_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(g_video_ani_canvas, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_move_to_index(g_video_ani_canvas, -1);
    MLOG_INFO("[VANIP] Overlay created\n");
}

/* 启动服务 */
static int video_start_anip_service(void)
{
    if (g_video_anip_handle >= 0) {
        MLOG_INFO("[VANIP] Already started\n");
        return 0;
    }

    ANIP_SERVICE_PARAM_S param = {0};
    param.in_vpss_grp = 1;
    param.in_vpss_chn = 0;
    param.in_width = 640;
    param.in_height = 480;
    param.osd_mirror = 0;
    param.sensitivity = 30;
    param.max_results = VIDEO_MAX_ANIMAL_BOXES;
    param.det_enable = 1;
    param.rec_enable = 1;
    param.osd_id = 1;

    ANIP_SERVICE_Register_DrawRects_Callback(video_anip_draw_rects_callback);
    ANIP_SERVICE_Register_Result_Callback(video_anip_result_callback);

    CVI_S32 ret = ANIP_SERVICE_Create(&g_video_anip_handle, &param);
    if (ret != 0) {
        MLOG_ERR("[VANIP] Create failed: %d\n", ret);
        ANIP_SERVICE_Unregister_DrawRects_Callback();
        ANIP_SERVICE_Unregister_Result_Callback();
        g_video_anip_handle = -1;
        return -1;
    }

    g_video_anip_enabled = true;
    MLOG_INFO("[VANIP] Service started, handle=%d\n", g_video_anip_handle);
    return 0;
}

/* 停止服务 */
static void video_stop_anip_service(void)
{
    g_video_anip_enabled = false;
    if (g_video_anip_handle >= 0) {
        ANIP_SERVICE_Clear_Rects(g_video_anip_handle);
        ANIP_SERVICE_Destroy(g_video_anip_handle);
        g_video_anip_handle = -1;
    }
    ANIP_SERVICE_Unregister_DrawRects_Callback();
    ANIP_SERVICE_Unregister_Result_Callback();
    MLOG_INFO("[VANIP] Service stopped\n");
}

/* AI按键回调 - 切换识别 */
static void video_play_callback(void)
{
    if (g_video_anip_enabled) {
        MLOG_INFO("[VANIP] Turning OFF\n");
        video_stop_anip_service();
        /* 销毁覆盖层 */
        if (g_video_ani_canvas != NULL) {
            if (lv_obj_is_valid(g_video_ani_canvas)) {
                lv_obj_del(g_video_ani_canvas);
            }
            g_video_ani_canvas = NULL;
        }
        for (int i = 0; i < VIDEO_MAX_ANIMAL_BOXES; i++) {
            if (g_video_ani_boxes[i].label != NULL) {
                if (lv_obj_is_valid(g_video_ani_boxes[i].label)) {
                    lv_obj_del(g_video_ani_boxes[i].label);
                }
                g_video_ani_boxes[i].label = NULL;
            }
            g_video_ani_boxes[i].valid = false;
        }
    } else {
        MLOG_INFO("[VANIP] Turning ON\n");
        video_create_anip_overlay(obj_vedio_s);
        if (video_start_anip_service() != 0) {
            MLOG_ERR("[VANIP] Start failed\n");
            if (g_video_ani_canvas != NULL) {
                if (lv_obj_is_valid(g_video_ani_canvas)) {
                    lv_obj_del(g_video_ani_canvas);
                }
                g_video_ani_canvas = NULL;
            }
        }
    }
}

void Home_Vedio(lv_ui_t *ui)
{
    MLOG_DBG("loading obj_vedio_s...\n");
    // 创建主页面1 容器
    if(obj_vedio_s != NULL) {
        if(lv_obj_is_valid(obj_vedio_s)) {
            MLOG_DBG("obj_vedio_s 仍然有效，删除旧对象\n");
            lv_obj_del(obj_vedio_s);
        } else {
            MLOG_DBG("obj_vedio_s 已被自动销毁，仅重置指针\n");
        }
        obj_vedio_s = NULL;
    }
    extern uint8_t g_last_scr_mode;
    g_last_scr_mode = 2;

    obj_vedio_s = lv_obj_create(NULL);
    lv_obj_set_size(obj_vedio_s, H_RES, V_RES);

    lv_obj_set_scrollbar_mode(obj_vedio_s, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj_vedio_s, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj_vedio_s, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(obj_vedio_s, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(obj_vedio_s, gesture_event_handler, LV_EVENT_GESTURE, ui);

    // 录像模式按钮
    g_video_top_controls[0] = lv_imagebutton_create(obj_vedio_s);
    lv_obj_set_size(g_video_top_controls[0], 76, 50);
    show_image(g_video_top_controls[0], "shexiangmoshi.png");
    lv_obj_add_event_cb(g_video_top_controls[0], buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)2);

    // 分辨率按钮
    g_video_top_controls[1] = lv_imagebutton_create(obj_vedio_s);
    lv_obj_set_size(g_video_top_controls[1], 40, 40);
    show_image(g_video_top_controls[1], video_getRes_Icon());
    lv_obj_add_event_cb(g_video_top_controls[1], video_res_btn_click_cb, LV_EVENT_CLICKED, NULL);  // 添加点击事件

    // 红光亮级按钮
    g_video_top_controls[2] = lv_imagebutton_create(obj_vedio_s);
    lv_obj_set_size(g_video_top_controls[2], 40, 40);
    lv_obj_add_event_cb(g_video_top_controls[2], video_redlight_btn_click_cb, LV_EVENT_CLICKED, NULL);  // 添加点击事件

    // // ISO级别按钮
    // g_video_top_controls[3] = lv_imagebutton_create(obj_vedio_s);
    // lv_obj_set_size(g_video_top_controls[3], 38, 32);
    // char* iso_buf[] = {
    //     "ISO.png", "ISO 100.png", "ISO 200.png", "ISO 400.png",
    //     "ISO 800.png", "ISO 1600.png", "ISO 3200.png", "ISO 6400.png",
    // };
    // show_image(g_video_top_controls[3], iso_buf[get_iso_index()]);

    // 屏幕亮度按钮
    g_video_top_controls[4] = lv_imagebutton_create(obj_vedio_s);
    lv_obj_set_size(g_video_top_controls[4], 40, 40);
    char* brightness_buf[] = { "1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png" };
    show_image(g_video_top_controls[4], brightness_buf[get_curr_brightness()]);
    lv_obj_add_event_cb(g_video_top_controls[4], video_brightness_btn_click_cb, LV_EVENT_CLICKED, NULL);  // 添加点击事件


    // g_video_top_controls[5] = lv_imagebutton_create(obj_vedio_s);
    // lv_obj_set_size(g_video_top_controls[5], 50, 14);
    // char* ev_buf[] = { "EV+3.png", "EV+2.png", "EV+1.png", "EV0.png", "EV-1.png", "EV-2.png", "EV-3.png" };
    // show_image(g_video_top_controls[5], ev_buf[get_EV_Level()]);
    // lv_obj_set_style_image_recolor(g_video_top_controls[5], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    // lv_obj_set_style_image_recolor_opa(g_video_top_controls[5], LV_OPA_COVER, LV_PART_MAIN);


    // 初始设置红光亮级图片
    if (brightness_level > 6) {
        show_image(g_video_top_controls[2], red_light_image_level[6]);
    } else if (brightness_level > 0) {
        show_image(g_video_top_controls[2], red_light_image_level[brightness_level-1]);
    }

    // 初始布局更新
    update_video_top_controls_layout();

    // // 将video_red_level_s指向红光亮级按钮，保持向后兼容
    // video_red_level_s = g_video_top_controls[2];

    // 剩余录像时间
    label_available_video_s = lv_label_create(obj_vedio_s);
    lv_obj_add_style(label_available_video_s, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_available_video_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(label_available_video_s, "%s", video_Calculateremainingvideo());
    lv_label_set_long_mode(label_available_video_s, LV_LABEL_LONG_WRAP);
    lv_obj_align(label_available_video_s, LV_ALIGN_TOP_RIGHT, -104, 6);

    // sd
    img_sdonline_s = lv_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_sdonline_s, LV_ALIGN_TOP_RIGHT, -58, 0);
    lv_obj_set_size(img_sdonline_s, 40, 40);
    if(ui_common_cardstatus()) {
        show_image(img_sdonline_s, "icon_card_online.png");
    } else {
        show_image(img_sdonline_s, "icon_card_offline.png");
    }

    // batter
    img_batter_s = lv_imagebutton_create(obj_vedio_s);
    lv_obj_align(img_batter_s, LV_ALIGN_TOP_RIGHT, -8, 2);
    lv_obj_set_size(img_batter_s, 40, 40);
    show_image(img_batter_s,"充电.png");


    //缩放
    lv_obj_t *imgbtn_zoomout = lv_imagebutton_create(obj_vedio_s);
    lv_obj_align(imgbtn_zoomout, LV_ALIGN_LEFT_MID, 12, -42);
    lv_obj_set_size(imgbtn_zoomout, 40, 40);
    show_image(imgbtn_zoomout, "T.png");
    lv_obj_add_event_cb(imgbtn_zoomout, photo_zoom_event_cb, LV_EVENT_ALL, (void *)(intptr_t)2);

    lv_obj_t *imgbtn_zoomin = lv_imagebutton_create(obj_vedio_s);
    lv_obj_align(imgbtn_zoomin, LV_ALIGN_LEFT_MID, 12, 42);
    lv_obj_set_size(imgbtn_zoomin, 40, 40);
    show_image(imgbtn_zoomin, "W.png");
    lv_obj_add_event_cb(imgbtn_zoomin, photo_zoom_event_cb, LV_EVENT_ALL, (void *)(intptr_t)1);


    if (get_curr_cursor() != 0) {
        lv_obj_t *cursor = lv_img_create(obj_vedio_s);
        lv_obj_set_size(cursor, 180, 180);
        lv_obj_align(cursor, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_pad_all(cursor, 0, LV_STATE_DEFAULT);
        extern const char *cursor_image_array[];
        show_image(cursor, cursor_image_array[get_curr_cursor() -1]);
    }

    // menu
    lv_obj_t *img_menu = lv_button_create(obj_vedio_s);
    lv_obj_align(img_menu, LV_ALIGN_BOTTOM_LEFT, 6, 0);
    lv_obj_set_size(img_menu, 60, 60);
    show_image(img_menu, "menu.png");
    lv_obj_set_style_bg_opa(img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(img_menu, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    // 滤镜
    lv_obj_t *btn_effect = lv_button_create(obj_vedio_s);
    lv_obj_align(btn_effect, LV_ALIGN_BOTTOM_LEFT, 60,0);
    lv_obj_set_size(btn_effect, 60, 60);
    lv_obj_set_style_bg_opa(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // 透明背景
    lv_obj_set_style_pad_all(btn_effect, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_effect, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_effect, vedioEffect_Select_event_cb, LV_EVENT_CLICKED, NULL);

    img_effect_s = lv_img_create(btn_effect);
    lv_obj_set_size(img_effect_s, 40, 40);
    lv_obj_align(img_effect_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(img_effect_s, 0, LV_STATE_DEFAULT);
    show_image(img_effect_s, "颜色特效.png");


    label_Vedio_Durtime_s = lv_label_create(obj_vedio_s);
    lv_obj_add_style(label_Vedio_Durtime_s, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(label_Vedio_Durtime_s, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(label_Vedio_Durtime_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_Vedio_Durtime_s, LV_ALIGN_BOTTOM_MID, 0, -12);
    if(getSelect_Index() == TIME_FLAG_OFF) {
        lv_obj_add_flag(label_Vedio_Durtime_s, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t *album = lv_button_create(obj_vedio_s);
    lv_obj_align(album, LV_ALIGN_BOTTOM_RIGHT, -80, 0);
    lv_obj_set_size(album, 60, 60);
    lv_obj_set_style_bg_opa(album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(album, "photo_album.png");
    lv_obj_add_event_cb(album, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)4);


    // 退出按钮
    lv_obj_t *img_exit = lv_button_create(obj_vedio_s);
    lv_obj_align(img_exit, LV_ALIGN_BOTTOM_RIGHT, -6, 0);
    lv_obj_set_size(img_exit, 60, 60);
    show_image(img_exit, "exit.png");
    lv_obj_set_style_bg_opa(img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(img_exit, buttonVedio_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)3);

    //创建缩放UI
    create_zoom_bar(obj_vedio_s);

    // 闪烁圆点
    dot_red_s = lv_obj_create(obj_vedio_s);
    lv_obj_set_size(dot_red_s, 24, 24);
    lv_obj_align(dot_red_s, LV_ALIGN_BOTTOM_MID, -80, -14);
    lv_obj_set_style_radius(dot_red_s, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(dot_red_s, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(dot_red_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(dot_red_s, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(dot_red_s, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(dot_red_s, 0, LV_STATE_DEFAULT);

    /* 设置当前页面按键处理回调 */
    set_current_page_handler(takephoto_key_handler);
    takephoto_register_up_callback(video_redlight_callback);
    takephoto_register_down_callback(video_redlight_callback);
    takephoto_register_menu_callback(key_takephoto_menu_callback);
    takephoto_register_mode_callback(key_takephoto_mode_callback);
    takephoto_register_ok_callback(key_takephoto_ok_callback);
    takephoto_register_long_menu_callback(video_long_menu_callback);
    // takephoto_register_long_mode_callback(video_long_mode_callback);
    takephoto_register_zoomin_callback(video_zoomin_key_cb);
    takephoto_register_zoomout_callback(video_zoomout_key_cb);
    takephoto_register_left_callback(video_left_callback);
    takephoto_register_right_callback(video_right_callback);
    takephoto_register_play_callback(video_play_callback);
    takephoto_power_callback(key_takephoto_power_callback);
    //创建时间更新定时器
    if(date_timer_s == NULL) {
        date_timer_s = lv_timer_create(video_var_dynamic_update, 1000, ui);
    }
    // 立即执行一次更新
    lv_timer_ready(date_timer_s);
    lv_obj_update_layout(obj_vedio_s);

}

// 缩放按键事件处理
static void video_zoomin_key_cb(void)
{
    uint32_t new_level = get_zoom_level();
    // 设置放大比例
    MESSAGE_S Msg = {0};
    Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
    Msg.arg1      = 0;
    snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
    MODEMNG_SendMessage(&Msg);
    // 更新UI
    update_zoom_bar(new_level);
}

// 缩放按键事件处理
static void video_zoomout_key_cb(void)
{
    uint32_t new_level = get_zoom_level();
    // 设置放大比例
    MESSAGE_S Msg = {0};
    Msg.topic     = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
    Msg.arg1      = 0;
    snprintf((char *)Msg.aszPayload, 3, "%d", new_level);
    MODEMNG_SendMessage(&Msg);
    // 更新UI
    update_zoom_bar(new_level);
}

// left按键处理回调函数
static void video_left_callback(void)
{
    if(get_is_effect_exist() == true) {
        effect_Select_prev();
    }
}

// right按键处理回调函数
static void video_right_callback(void)
{
    if(get_is_effect_exist() == true) {
        effect_AISelect_next();
    }
}

// 长按定时器回调函数
static void zoom_longpress_timer_cb(lv_timer_t *timer)
{

    if (obj_vedio_s == NULL || !lv_obj_is_valid(obj_vedio_s)) {
        g_zoom_longpress_active = false;
        if (g_zoom_longpress_timer != NULL) {
            lv_timer_del(g_zoom_longpress_timer);
            g_zoom_longpress_timer = NULL;
        }
        return;
    }

    if (!g_zoom_longpress_active) {
        lv_timer_del(timer);
        g_zoom_longpress_timer = NULL;
        return;
    }

    uint32_t new_level = get_zoom_level();
    MESSAGE_S Msg = {0};
    bool can_continue = false;

    switch (g_zoom_longpress_dir) {
        case 1: // 缩小
            if (new_level > 1) {
                new_level--;
                can_continue = true;
            }
            break;

        case 2: // 放大
            if (new_level < ZOOM_RADIO_MAX) {
                new_level++;
                can_continue = true;
            }
            break;
    }

    if (can_continue) {
        set_zoom_level(new_level);
        new_level = get_zoom_level();

        Msg.topic = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
        Msg.arg1 = 0;
        snprintf((char*)Msg.aszPayload, 3, "%d", new_level);
        MODEMNG_SendMessage(&Msg);
        update_zoom_bar(new_level);

        MLOG_DBG("长按缩放: 方向=%d, 等级=%d\n", g_zoom_longpress_dir, new_level);
    } else {
        // 达到边界，停止长按
        g_zoom_longpress_active = false;
        lv_timer_del(timer);
        g_zoom_longpress_timer = NULL;
    }
}

static void photo_zoom_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int click_index = (int)lv_event_get_user_data(e);
    static uint32_t last_click_time = 0;

    if (obj_vedio_s == NULL || !lv_obj_is_valid(obj_vedio_s)) {
        g_zoom_longpress_active = false;
        if (g_zoom_longpress_timer != NULL) {
            lv_timer_del(g_zoom_longpress_timer);
            g_zoom_longpress_timer = NULL;
        }
        return;
    }

    switch(code) {
        case LV_EVENT_PRESSED: {
            // 立即执行一次缩放
            uint32_t new_level = get_zoom_level();
            MESSAGE_S msg = {0};
            // bool zoom_performed = false;

            if (click_index == 1 && new_level > 1) { // 缩小
                new_level--;
                // zoom_performed = true;
            } else if (click_index == 2 && new_level < ZOOM_RADIO_MAX) { // 放大
                new_level++;
                // zoom_performed = true;
            }

            // if (zoom_performed) {
                set_zoom_level(new_level);
                new_level = get_zoom_level();

                msg.topic = EVENT_MODEMNG_LIVEVIEW_ADJUSTFOCUS;
                msg.arg1 = 0;
                snprintf((char*)msg.aszPayload, 3, "%d", new_level);
                MODEMNG_SendMessage(&msg);
                update_zoom_bar(new_level);

                MLOG_DBG("缩放按钮按下: 方向=%d, 新等级=%d\n", click_index, new_level);
            // }

            // 记录按下时间
            last_click_time = lv_tick_get();

            // 启动长按定时器
            g_zoom_longpress_dir = click_index;
            g_zoom_longpress_active = true;
            if (g_zoom_longpress_timer == NULL) {
                g_zoom_longpress_timer = lv_timer_create(zoom_longpress_timer_cb, 100, NULL);
            }
            break;
        }

        case LV_EVENT_RELEASED: {
            // 停止长按
            g_zoom_longpress_active = false;
            if (g_zoom_longpress_timer != NULL) {
                lv_timer_del(g_zoom_longpress_timer);
                g_zoom_longpress_timer = NULL;
            }

            // 计算按下时间
            uint32_t press_duration = lv_tick_get() - last_click_time;

            // 如果是短按（小于300ms），不执行额外操作
            if (press_duration < 300) {
                MLOG_DBG("短按释放: 持续时间=%dms\n", press_duration);
            } else {
                MLOG_DBG("长按释放: 持续时间=%dms\n", press_duration);
            }
            break;
        }
        default:
        break;
    }
}