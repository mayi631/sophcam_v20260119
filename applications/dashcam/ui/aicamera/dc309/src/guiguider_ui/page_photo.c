#define DEBUG

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "config.h"
#include "custom.h"
#include "page_all.h"
#include "ui_common.h"
#include <time.h>
#include "indev.h"
#include "common/takephoto.h"
#include "img2img/img2img.h"
#include "indev.h"
#include "common/extract_thumbnail.h"
#include "face_beautifier/face_beautifier.h"
#include "image_process.h"
#include "jpegp.h"
#include "cvi_comm_vo.h"
#include "cvi_vo.h"
#include "facep_service.h"
#include "anip_service.h"
#include "media_init.h"
#include "media_osd.h"
#include "liveview.h"
#include "linux/input.h"
#include "rtt.h"
#include "infrared.h"
#include "icon_select_popup.h"
#include "page_photomenu_res.h"
#include "page_sysmenu_brightness.h"
#include "delete_dialog.h"
#include "kt_ani_api.h"
#include "animal_labels.h"
#include "mlog.h"

// 动物识别相关宏定义
#define MAX_ANIMAL_BOXES    5         // 最大同时显示的识别框数量

// 动物识别框结构体
typedef struct {
    lv_obj_t *label;           // 标签
    bool valid;                // 是否有效
} animal_box_t;

// 控件状态结构体
typedef struct {
    lv_obj_t *obj;
    bool hidden;
} widget_state_t;

// 全局变量用于存储控件状态
static widget_state_t *g_widget_states = NULL;
static int g_widget_count = 0;
static int g_max_widgets = 0;
static bool g_widgets_hidden = false; // 标记是否已隐藏但未恢复

extern lv_style_t ttf_font_24;
static char pic_thumbnail[128] = { 0 };

#define AI_OUT_IMG_WIDTH 960
#define AI_OUT_IMG_HEIGHT 720
#define SUBPIC_WIDTH 640
#define SUBPIC_HEIGHT 480
#define THUMBNAIL_WIDTH 200
#define THUMBNAIL_HEIGHT 140

// 全局变量声明
static lv_timer_t *date_timer_s    = NULL;    // 更新定时器
static uint8_t limit_key_flag_s    = true;    // 限制自拍时间内的连续按键按下标志
static lv_obj_t *continuous_count_label_s;           // 连续拍照拍照次数
static bool continue_limit_key_flag_s = true; // 限制连续拍照时间内的连续按键按下标志
static lv_obj_t *label_delay_time_s = NULL;     //延时拍摄倒计时文本
static lv_timer_t *continue_take_photo_timer = NULL;//连续拍照定时器
// 延时拍照，用于跟踪上一次的整秒值
static int last_second = -1;
static bool is_normal_capturing = false; // 普通拍照进行中标志
static bool power_key_count = false; // 电源键切换图标状态标志

extern bool is_video_mode;
extern bool led_on_flag;
extern const char *effect_style_small[];//特效图片数组
static lv_obj_t *img_effect_s = NULL;  //特效图标
// i是否返回photo页面
static bool is_photo_back = true;

const char *batter_image_big[] = {"充电.png", "电池0.png", "电池1.png", "电池2.png","电池满.png"};
char *red_light_image_level[] = {"IR 1.png", "IR 2.png", "IR 3.png", "IR 4.png","IR 5.png", "IR 6.png", "IR 7.png"};

// 光标图片数组 [光标类型][颜色: 0=Green, 1=Red, 2=Yellow]
const char* cursor_image_array[] = {
    "Icon_1_cross_Red.png",
    // "Icon_2_cross_Red.png",
    // "Icon_3_cross_RED.png",
    "Icon_4_cross_RED.png",
    "Icon_5_cross_Red.png",
    "Icon_6_cross_Red.png",
    "Icon_7_cross_Red.png",
    "Icon_8_cross_Red.png",
};
static lv_obj_t *g_top_controls[7];  // 存储顶部控件对象

static lv_timer_t *g_zoom_longpress_timer = NULL;  // 长按定时器
static int g_zoom_longpress_dir = 0;               // 长按方向: 0=无, 1=缩小, 2=放大
static bool g_zoom_longpress_active = false;       // 是否正在长按

// 实时动物检测相关全局变量
static ANIP_SERVICE_HANDLE_T g_anip_handle = -1;   // 动物识别服务句柄
static lv_obj_t *s_ani_canvas = NULL;              // 动物检测框绘制画布
static bool s_anip_enabled = false;                // 实时动物检测是否启用
static animal_box_t s_ani_boxes[MAX_ANIMAL_BOXES] = {0}; // 动物标签对象
#define ANI_LABEL_BG_COLOR lv_color_hex(0x8000FF00) // 半透明绿色背景

// 资源释放函数声明
static void release_HomePhoto_resources(lv_ui_t *ui);

static void photo_zoom_event_cb(lv_event_t* e);

void photo_process_ai_beauty(void); //美颜处理
static void photoScroll_del_cb(void);//删除浮窗

static void zoomin_key_cb(void);//t按键回调
static void zoomout_key_cb(void);//w按键回调
void hide_all_widgets(lv_obj_t *parent);
void restore_all_widgets(void);
void register_all_key(void);                                     // 注册按键

static void photoEffect_Select_event_cb(lv_event_t *e);
void continue_take_photo(void);

static void restore_icon_on_any_key(void);

// 图标选择弹窗回调声明（LVGL事件回调格式）
static void icon_select_res_callback(lv_event_t *e);
static void icon_select_redlight_callback(lv_event_t *e);
static void icon_select_brightness_callback(lv_event_t *e);
static void icon_select_shootmode_callback(lv_event_t *e);

// 实时动物检测相关函数声明
static int anip_draw_rects_callback(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects);
static void anip_result_callback(CVI_U32 osd_id, ANIP_RESULT_S* results, CVI_U32 count);
static void anip_result_ui_update(void *user_data);
static void create_anip_overlay(lv_obj_t *parent);
static void destroy_anip_overlay(void);
static int start_anip_service(void);
static void stop_anip_service(void);

bool get_is_photo_back(void)
{
    return is_photo_back;
}

void set_is_photo_back(bool is_back)
{
    is_photo_back = is_back;
}

// 简洁的顶部控件布局更新
static void update_top_controls_simple(void)
{
    lv_ui_t *ui = &g_ui;
    int x_pos = 6;  // 起始X坐标
    
    // 1. 相机模式按钮
    lv_obj_set_pos(ui->page_photo.img_mode, x_pos, 0);
    x_pos += 76 + 10;
    
    // 2. 分辨率按钮
    lv_obj_set_pos(g_top_controls[0], x_pos, 4);
    x_pos += 40 + 10;
    
    // 3. 红光亮级按钮
    if (brightness_level > 0) {
        lv_obj_clear_flag(ui->page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ui->page_photo.redlight_level, x_pos, 4);
        x_pos += 40 + 10;
    } else {
        lv_obj_add_flag(ui->page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
    }
    
    // // 4. ISO级别按钮
    // lv_obj_set_pos(g_top_controls[1], x_pos, 4);
    // x_pos += 40 + 10;
    
    // 5. 屏幕亮度按钮
    lv_obj_set_pos(g_top_controls[2], x_pos, 4);
    x_pos += 40 + 10;
    
    // 6. 连拍按钮
    lv_obj_set_pos(g_top_controls[3], x_pos, 4);
    x_pos += 40 + 10;
    if (get_shootmode(0) == 0) {
        lv_obj_add_flag(g_top_controls[3], LV_OBJ_FLAG_HIDDEN);
    }
}

const char *get_ai_process_result_img_data(bool is_aiprocess)
{
    MLOG_DBG("show pic: %s\n", pic_filepath);
    char thumbnail_path_small[256];
    char thumbnail_path_large[256];
    char *filename = strrchr(pic_filepath, '/');
    if(strstr(filename, ".jpg")) {
        get_thumbnail_path(pic_filepath, thumbnail_path_small, sizeof(thumbnail_path_small), PHOTO_SMALL_PATH);
        get_thumbnail_path(pic_filepath, thumbnail_path_large, sizeof(thumbnail_path_large), PHOTO_LARGE_PATH);

        strncpy(pic_thumbnail, thumbnail_path_large, sizeof(pic_thumbnail));
        if(is_aiprocess) {
            char *real_large = strchr(pic_thumbnail, '/');
            return real_large;
        }
    }
    return pic_thumbnail;
}

// 参数动态更新回调
int32_t g_batter_image_index = 4;

void set_batter_image_index(int32_t index)
{
    if(index < 0 || index > 4) {
        g_batter_image_index = 4; // 默认满电
        return;
    }

    switch (index) {
    case 0:
        g_batter_image_index = 4;
        break; // 电量满电
    case 1:
        g_batter_image_index = 3;
        break; // 电量2
    case 2:
        g_batter_image_index = 2;
        break; // 电量低电
    case 3:
        g_batter_image_index = 1;
        break; // 充电
    case 4:
        g_batter_image_index = 0;
        break; 
    default:
        g_batter_image_index = 0;
        break;
    }

    MLOG_DBG("batter index:%d, g_batter_image_index:%d\n", index, g_batter_image_index);

    // 当电池电量变化时，自动调整红外灯档位
    auto_adjust_redlight_by_battery();
}

static void photo_var_dynamic_update(lv_timer_t *timer)
{
    lv_ui_t *ui      = (lv_ui_t *)lv_timer_get_user_data(timer);
    time_t now       = time(NULL);
    struct tm *t     = localtime(&now);

    lv_obj_t *page_photo = ui->page_photo.photoscr;

    // 检查对象有效性，避免在页面切换时访问已销毁的对象
    if(page_photo == NULL && !lv_obj_is_valid(page_photo)) {
        if(date_timer_s != NULL) {
            lv_timer_del(date_timer_s);
            date_timer_s = NULL;
        }
        return;
    }

    if(ui->page_photo.label_datatime == NULL || !lv_obj_is_valid(ui->page_photo.label_datatime)) return;
    // 时间更新
    // MLOG_DBG("BUG调试 ui->page_photo.label_datatime%p %d\n",ui->page_photo.label_datatime,lv_obj_is_valid(ui->page_photo.label_datatime));
    lv_label_set_text_fmt(ui->page_photo.label_datatime, "%04d/%02d/%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday,t->tm_hour, t->tm_min, t->tm_sec);
    show_image(ui->page_photo.img_batter, batter_image_big[g_batter_image_index]);
    lv_label_set_text_fmt(ui->page_photo.label_numphoto, "%02d", photo_CalculateRemainingPhotoCount());

    if(ui_common_cardstatus()) {
        show_image(ui->page_photo.img_sdonline, "icon_card_online.png");
    } else {
        show_image(ui->page_photo.img_sdonline, "icon_card_offline.png");
    }
}

// 拍照完成
void photo_resume_anim_complete(lv_anim_t *a)
{
    if(AIModeSelect_GetMode() == AI_NONE) {
        restore_all_widgets();
        lv_timer_resume(date_timer_s);
    }
    is_normal_capturing = false; // 清除普通拍照标志
    CVI_VO_ResumeChn(0,0);
    lv_anim_del(a, a->exec_cb);
}

//延时拍计数动画
void photo_delay_anim(void *var, int32_t v)
{
    // 计算当前剩余的整秒数（向下取整）
    int current_second = get_self_delay_time() - (int)v;

    // 只有当整秒数变化时才更新标签
    if(current_second != last_second) {
        // 确保标签可见
        if(lv_obj_has_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN);
        }
        // 更新标签文本
        lv_label_set_text_fmt(label_delay_time_s, "%02d", current_second);
        // 更新记录的最后整秒值
        last_second = current_second;

        if (current_second != get_self_delay_time()) {
            // 触发倒计时声音
            EVENT_S stEvent = { 0 };
            stEvent.topic = EVENT_UI_TOUCH;
            EVENTHUB_Publish(&stEvent);
        }
        // 调试信息
        MLOG_DBG("倒计时更新: %d\n", current_second);
    }
}
//延时拍计数动画完成
void photo_delay_anim_complete(lv_anim_t *a)
{
    limit_key_flag_s = true;
    if(!lv_obj_has_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN);
    }
    if(get_shootmode(1) == 0) {
        hide_all_widgets(g_ui.page_photo.photoscr);
        lv_timer_pause(date_timer_s);//暂停动画更新
        CVI_VO_PauseChn(0, 0);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_values(&anim, 0, 1);
        lv_anim_set_time(&anim, 1000);
        lv_anim_set_path_cb(&anim, lv_anim_path_linear); // 使用线性路径
        lv_anim_set_completed_cb(&anim, photo_resume_anim_complete);
        lv_anim_start(&anim);

        MESSAGE_S Msg = {0};
        Msg.topic     = EVENT_MODEMNG_START_PIV;
        MODEMNG_SendMessage(&Msg);
        ui_common_wait_piv_end();
        enable_touch_events();//倒计时结束，没有连拍，恢复触摸
        enable_hardware_input_device(0);
        enable_hardware_input_device(1);
    } else {//倒计时结束，开始连拍
        if(continue_limit_key_flag_s) {
            hide_all_widgets(g_ui.page_photo.photoscr);
            lv_timer_pause(date_timer_s);
            if(lv_obj_has_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN);
            }
            // 开始连拍
            continue_take_photo();
            continue_limit_key_flag_s = false;
        }
    }
    lv_anim_del(a, a->exec_cb);
}
//拍照之前回调
static void key_takephoto_before_exec(void* user_data)
{
    is_normal_capturing = true; // 标记普通拍照开始
    //如果发生连续点击，先恢复原有的状态再隐藏//防止将原有的显示状态覆盖掉
    if(!lv_obj_has_flag(g_ui.page_photo.img_sdonline, LV_OBJ_FLAG_HIDDEN) && (AIModeSelect_GetMode() == AI_NONE)) {
        hide_all_widgets(g_ui.page_photo.photoscr);
    } else if(AIModeSelect_GetMode() == AI_NONE) {
        restore_all_widgets();
        lv_timer_resume(date_timer_s);
        hide_all_widgets(g_ui.page_photo.photoscr);
        lv_timer_pause(date_timer_s);
    }
    CVI_VO_PauseChn(0, 0);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_values(&anim, 0, 1);
    lv_anim_set_time(&anim, 1000);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear); // 使用线性路径
    lv_anim_set_completed_cb(&anim, photo_resume_anim_complete);
    lv_anim_start(&anim);
}

//拍照之前回调
static void key_takephoto_before_callback(void)
{
    lv_async_call(key_takephoto_before_exec, NULL);
}
//按键执行回调
static void key_takephoto_callback_exec(void* user_data)
{
    restore_icon_on_any_key(); // 任意键恢复图标
    set_defalt_retval();
    is_normal_capturing = true; // 标记普通拍照开始
    //如果有倒计时，且ai为普通模式，则进入延时拍
    if(get_self_delay_time() != 0 && AIModeSelect_GetMode() == AI_NONE) {
        if(limit_key_flag_s) {
            limit_key_flag_s = false;
            // 重置整秒记录
            last_second = -1;
            // 立即显示初始值
            lv_obj_clear_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text_fmt(label_delay_time_s, "%02d", get_self_delay_time());
            // 调试信息
            MLOG_DBG("次数：get_self_time():%d 总时长:%d\n", get_self_delay_time(), get_self_delay_time() * 1000);
            cancel_viewfinder();
            disable_touch_events();//延时倒计时，禁用触摸
            disable_hardware_input_device(0);
            disable_hardware_input_device(1);
            // 创建动画
            lv_anim_t anim;
            lv_anim_init(&anim);
            lv_anim_set_values(&anim, 0, get_self_delay_time());
            lv_anim_set_time(&anim, get_self_delay_time() * 1000);
            lv_anim_set_exec_cb(&anim, photo_delay_anim);
            lv_anim_set_path_cb(&anim, lv_anim_path_linear); // 使用线性路径
            lv_anim_set_completed_cb(&anim, photo_delay_anim_complete);
            lv_anim_start(&anim);
        }
    } else {
        // 如果有连拍和ai为普通模式，则进入连拍
        if(get_shootmode(1) != 0 && AIModeSelect_GetMode() == AI_NONE) {
            if(continue_limit_key_flag_s) {
                hide_all_widgets(g_ui.page_photo.photoscr);
                lv_timer_pause(date_timer_s);
                if(lv_obj_has_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN)) {
                    lv_obj_clear_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN);
                }
                disable_touch_events();//连拍中，禁用触摸
                disable_hardware_input_device(0);
                disable_hardware_input_device(1);
                // 创建连续拍照动画
                continue_take_photo();
                continue_limit_key_flag_s = false;
            }
        }
    }
}

//按键执行回调
static void key_takephoto_callback(void)
{
    lv_async_call(key_takephoto_callback_exec, NULL);
}

// 任意键恢复图标显示
static void restore_icon_on_any_key(void)
{
    if (power_key_count) {
        power_key_count = false;
        restore_all_widgets();
    }
}

// 删除浮窗
static void photoScroll_del_cb(void)
{
    delete_aiselect_scroll();
}

// ok按键处理回调函数
static void photo_sesor_switch_completed_callback(void)
{
    enable_touch_events(); // 连拍结束，恢复触摸
    enable_hardware_input_device(0);
    enable_hardware_input_device(1);
}

// 所有事件回调处理
static void buttonPhoto_All_event_handler(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int Click_index      = (int)lv_event_get_user_data(e);
    MLOG_DBG("code:%d, Click_index:%d\n", code, Click_index);

    if(code == LV_EVENT_CLICKED) {
        lv_ui_t *ui   = &g_ui;
        MESSAGE_S Msg = {0};

        switch(Click_index) {
            case 0: // 跳转系统菜单
                release_HomePhoto_resources(ui);
                ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
                                      &g_ui.screenHomePhoto_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                                      true);
                break;

            case 1: // 切换到视频模式
                release_HomePhoto_resources(ui);
                homeMode_Set(VEDIO_MODE);
                // 进入录像模式
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1  = WORK_MODE_MOVIE;
                MODEMNG_SendMessage(&Msg);
                // 复位缩放
                set_zoom_level(1);
                // 关闭对焦
                disable_focus();
                is_video_mode = true;
                ui_load_scr_animation(&g_ui, &obj_vedio_s, 1, &g_ui.screenHomePhoto_del, Home_Vedio,
                                      LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                break;


            case 9: // 返回主页
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
                release_HomePhoto_resources(ui);
                ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                                      true);
                //关闭红外参数
                brightness_level = 0;
                led_off();
                ircut_off();
                led_on_flag = false;

                reset_effect();
                break;
            case 3: //相册
                release_HomePhoto_resources(ui);
                // 复位缩放
                set_zoom_level(1);
                // 关闭对焦
                disable_focus();
                // 进入录像模式
                MESSAGE_S Msg = { 0 };
                Msg.topic = EVENT_MODEMNG_MODESWITCH;
                Msg.arg1 = WORK_MODE_PLAYBACK;
                MODEMNG_SendMessage(&Msg);
                ui_load_scr_animation(&g_ui, &obj_Aibum_s, 1, NULL, Home_Album, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
                break;
            default: break;
        }
    }
}

// 初始化事件
static void events_init_HomePhoto(lv_ui_t *ui)
{
    lv_obj_add_event_cb(ui->page_photo.img_mode, buttonPhoto_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)1);
    lv_obj_add_event_cb(ui->page_photo.img_exit, buttonPhoto_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)9);
    lv_obj_add_event_cb(ui->page_photo.img_album, buttonPhoto_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)3);
    lv_obj_add_event_cb(ui->page_photo.img_menu, buttonPhoto_All_event_handler, LV_EVENT_CLICKED, (void *)(intptr_t)0);
    
    // 添加图标选择弹窗点击事件
    lv_obj_add_event_cb(g_top_controls[0], icon_select_res_callback, LV_EVENT_CLICKED, NULL);  // 分辨率
    lv_obj_add_event_cb(ui->page_photo.redlight_level, icon_select_redlight_callback, LV_EVENT_CLICKED, NULL);  // 红外灯亮度
    lv_obj_add_event_cb(g_top_controls[2], icon_select_brightness_callback, LV_EVENT_CLICKED, NULL);  // 屏幕亮度
    lv_obj_add_event_cb(g_top_controls[3], icon_select_shootmode_callback, LV_EVENT_CLICKED, NULL);  // 连拍模式
}

// 资源释放函数
static void release_HomePhoto_resources(lv_ui_t *ui)
{
    // 删除AI选择滚动容器
    delete_aiselect_scroll(); // ai选择弹窗
    delete_all_handle(); // 特效选择弹窗
    delete_viewfinder(); // 取景框
    delete_zoom_bar(); // 放大缩小
    wifi_check_dialog_close(); // wifi是否开启检查对话框销毁
    delete_batter_tips_mbox(); // 低电量不允许开wifi弹窗销毁
    destroy_voice_input_popup(); // ai语音自定义弹窗销毁
    delete_icon_select_popup(); // 图标选择弹窗销毁（停止隐藏动画）

    if(date_timer_s != NULL) {
        lv_timer_del(date_timer_s);
        date_timer_s = NULL;
    }

    if(continue_take_photo_timer != NULL) {
        lv_timer_del(continue_take_photo_timer);
        continue_take_photo_timer = NULL;
    }
    g_widgets_hidden = false;
    power_key_count = false;
    // 释放缩放相关资源
    delete_zoombar_timer_handler();
    // 删除当前页面按键处理回调
    takephoto_unregister_all_callback();
    FACEP_SERVICE_Unregister_Smile_Pre_Callback();
    FACEP_SERVICE_Unregister_Smile_Post_Callback();
    set_current_page_handler(NULL);

    // 停止实时动物检测服务
    stop_anip_service();
    destroy_anip_overlay();
}

// 菜单按键处理回调函数
static void photo_menu_callback(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标
    MLOG_DBG("进入拍照模式设置页面\n");
    release_HomePhoto_resources(&g_ui);
    ui_load_scr_animation(&g_ui, &g_ui.page_photoMenu_Setting.menuscr, g_ui.screenPhotoMenuSetting_del,
        &g_ui.screenHomePhoto_del, photoMenu_Setting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
        true);
}

// UP/DOWN按键处理回调函数（红光亮级更新）
static void photo_redlight_callback(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标
    // 调用通用的红光UI更新函数
    update_redlight_ui();
}

// 模式切换按键处理回调函数
static void photo_mode_callback(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标
    MLOG_DBG("AI按键,进入AI模式切换页面\n");
    MLOG_DBG("模式切换，进入视频模式\n");
    MESSAGE_S Msg = {0};
    release_HomePhoto_resources(&g_ui);
    homeMode_Set(VEDIO_MODE);
    // 进入录像模式
    Msg.topic = EVENT_MODEMNG_MODESWITCH;
    Msg.arg1  = WORK_MODE_MOVIE;
    MODEMNG_SendMessage(&Msg);
    // 复位缩放
    set_zoom_level(1);
    // 关闭对焦
    disable_focus();
    is_video_mode = true;
    ui_load_scr_animation(&g_ui, &obj_vedio_s, 1, &g_ui.screenHomePhoto_del, Home_Vedio, LV_SCR_LOAD_ANIM_NONE,
                            0, 0, false, true);
}

// AI按键处理回调函数 - 切换实时动物检测的打开/关闭
static void photo_play_callback(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标

    extern bool is_animal_recognition_page;
    if (!is_animal_recognition_page)
        return;

    if (s_anip_enabled) {
        /* 当前已开启，关闭检测 */
        MLOG_INFO("[ANIP] Turning OFF animal detection\n");
        stop_anip_service();
        destroy_anip_overlay();
    } else {
        /* 当前已关闭，开启检测 */
        MLOG_INFO("[ANIP] Turning ON animal detection\n");
        create_anip_overlay(g_ui.page_photo.photoscr);
        if (start_anip_service() != 0) {
            MLOG_ERR("[ANIP] Failed to start animal detection service\n");
            destroy_anip_overlay();
        }
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
                        Msg.topic = EVENT_MODEMNG_SENSOR_STATE;
                        Msg.arg1  = 1;
                        MODEMNG_SendMessage(&Msg);
                        memset(&Msg, 0, sizeof(MESSAGE_S));
                        // 进入BOOT模式
                        Msg.topic = EVENT_MODEMNG_MODESWITCH;
                        Msg.arg1  = WORK_MODE_BOOT;
                        MODEMNG_SendMessage(&Msg);
                        // 复位缩放
                        set_zoom_level(1);
                        release_HomePhoto_resources(&g_ui);
                        ui_load_scr_animation(&g_ui, &obj_home_s, 1, NULL, setup_scr_home1, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                                              false, true);
                        reset_effect();
                        brightness_level = 0;
                        led_off();
                        ircut_off();
                        led_on_flag = false;
                        if(date_timer_s != NULL) {
                            lv_timer_del(date_timer_s);
                            date_timer_s = NULL;
                        }
                }
                default: break;
            }
            break;
        }
        default: break;
    }
}

// 创建主页面
void Home_Photo(lv_ui_t *ui)
{
    MLOG_DBG("loading page_home1...\n");
    HomePhoto_t *Home_Photo_Item = &ui->page_photo;
    is_photo_back = true;
    extern uint8_t g_last_scr_mode;
    g_last_scr_mode = 1;
    set_exit_completed(false);
    
    // 创建新页面
    Home_Photo_Item->photoscr = lv_obj_create(NULL);
    lv_obj_set_size(Home_Photo_Item->photoscr, H_RES, V_RES);
    lv_obj_set_scrollbar_mode(Home_Photo_Item->photoscr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(lv_layer_bottom(), LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(Home_Photo_Item->photoscr, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(Home_Photo_Item->photoscr, lv_color_hex(0x020524), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(Home_Photo_Item->photoscr, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(Home_Photo_Item->photoscr, gesture_event_handler, LV_EVENT_GESTURE, ui);
    
    // 相机模式按钮
    Home_Photo_Item->img_mode = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_set_size(Home_Photo_Item->img_mode, 76, 50);
    show_image(Home_Photo_Item->img_mode, "paizhaompshi.png");
    
    // 分辨率按钮
    g_top_controls[0] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_set_size(g_top_controls[0], 40, 40);
    show_image(g_top_controls[0], photo_getRes_Icon());
    
    // 红光亮级按钮
    Home_Photo_Item->redlight_level = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_set_size(Home_Photo_Item->redlight_level, 40, 40);
    
    // ISO级别按钮
    // g_top_controls[1] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    // lv_obj_set_size(g_top_controls[1], 38, 32);
    // char* iso_buf[] = {
    //     "ISO.png", "ISO 100.png", "ISO 200.png", "ISO 400.png",
    //     "ISO 800.png", "ISO 1600.png", "ISO 3200.png", "ISO 6400.png",
    // };
    // show_image(g_top_controls[1], iso_buf[get_iso_index()]);
    
    // 屏幕亮度按钮
    g_top_controls[2] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_set_size(g_top_controls[2], 40, 40);
    char* brightness_buf[] = { "1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png" };
    show_image(g_top_controls[2], brightness_buf[get_curr_brightness()]);
    
    // 连拍按钮
    g_top_controls[3] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_set_size(g_top_controls[3], 40, 40);
    char* continue_buf[] = { "连拍关闭.png", "连拍3.png", "连拍5.png", "连拍7.png" };
    show_image(g_top_controls[3], continue_buf[get_shootmode(0)]);
    
    // // 延时拍摄按钮
    // g_top_controls[4] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    // lv_obj_set_size(g_top_controls[4], 38, 33);
    // char* delay_buf[] = { "延时 关闭.png", "延时5.png", "延时7.png", "延时10.png" };
    // show_image(g_top_controls[4], delay_buf[get_self_index()]);
    
    // g_top_controls[5] = lv_imagebutton_create(Home_Photo_Item->photoscr);
    // lv_obj_set_size(g_top_controls[5], 50, 14);
    // char* ev_buf[] = { "EV+3.png", "EV+2.png", "EV+1.png", "EV0.png", "EV-1.png", "EV-2.png", "EV-3.png" };
    // show_image(g_top_controls[5], ev_buf[get_EV_Level()]);
    // lv_obj_set_style_image_recolor(g_top_controls[5], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    // lv_obj_set_style_image_recolor_opa(g_top_controls[5], LV_OPA_COVER, LV_PART_MAIN);


    // 初始设置红光亮级图片
    if (brightness_level > 6) {
        show_image(Home_Photo_Item->redlight_level, red_light_image_level[6]);
    } else if (brightness_level > 0) {
        show_image(Home_Photo_Item->redlight_level, red_light_image_level[brightness_level-1]);
    }
    
    // 初始布局更新
    update_top_controls_simple();

    // 剩余拍照数量
    Home_Photo_Item->label_numphoto = lv_label_create(Home_Photo_Item->photoscr);
    lv_label_set_text_fmt(Home_Photo_Item->label_numphoto, "%02d", photo_CalculateRemainingPhotoCount());
    lv_label_set_long_mode(Home_Photo_Item->label_numphoto, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(Home_Photo_Item->label_numphoto, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(Home_Photo_Item->label_numphoto, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(Home_Photo_Item->label_numphoto, LV_ALIGN_TOP_RIGHT, -104, 6);

    // SD卡状态
    Home_Photo_Item->img_sdonline = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_align(Home_Photo_Item->img_sdonline, LV_ALIGN_TOP_RIGHT, -58, 0);
    lv_obj_set_size(Home_Photo_Item->img_sdonline, 40, 40);
    if(ui_common_cardstatus()) {
        show_image(Home_Photo_Item->img_sdonline, "icon_card_online.png");
    } else {
        show_image(Home_Photo_Item->img_sdonline, "icon_card_offline.png");
    }

    // 电池状态
    Home_Photo_Item->img_batter = lv_imagebutton_create(Home_Photo_Item->photoscr);
    lv_obj_align(Home_Photo_Item->img_batter, LV_ALIGN_TOP_RIGHT, -8, 2);
    lv_obj_set_size(Home_Photo_Item->img_batter, 40, 40);
    show_image(Home_Photo_Item->img_batter,"充电.png");

    //缩放
    lv_obj_t *imgbtn_zoomout = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(imgbtn_zoomout, LV_ALIGN_LEFT_MID, 6, -42);
    lv_obj_set_size(imgbtn_zoomout, 60, 60);
    lv_obj_set_style_bg_opa(imgbtn_zoomout, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(imgbtn_zoomout, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(imgbtn_zoomout, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(imgbtn_zoomout, "T.png");
    lv_obj_add_event_cb(imgbtn_zoomout, photo_zoom_event_cb, LV_EVENT_ALL, (void *)(intptr_t)2);

    lv_obj_t *imgbtn_zoomin = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(imgbtn_zoomin, LV_ALIGN_LEFT_MID, 6, 42);
    lv_obj_set_size(imgbtn_zoomin, 60, 60);
    lv_obj_set_style_bg_opa(imgbtn_zoomin, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(imgbtn_zoomin, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(imgbtn_zoomin, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(imgbtn_zoomin, "W.png");
    lv_obj_add_event_cb(imgbtn_zoomin, photo_zoom_event_cb, LV_EVENT_ALL, (void *)(intptr_t)1);

    if (get_curr_cursor() != 0) {
        lv_obj_t *cursor = lv_img_create(Home_Photo_Item->photoscr);
        lv_obj_set_size(cursor, 180, 180);
        lv_obj_align(cursor, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_pad_all(cursor, 0, LV_STATE_DEFAULT);
        show_image(cursor, cursor_image_array[get_curr_cursor() - 1]);
    }

    // 菜单按钮
    Home_Photo_Item->img_menu = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(Home_Photo_Item->img_menu, LV_ALIGN_BOTTOM_LEFT, 6, 0);
    lv_obj_set_size(Home_Photo_Item->img_menu, 60, 60);
    lv_obj_set_style_bg_opa(Home_Photo_Item->img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(Home_Photo_Item->img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(Home_Photo_Item->img_menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(Home_Photo_Item->img_menu, "menu.png");

    // 滤镜
    lv_obj_t *btn_effect = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(btn_effect, LV_ALIGN_BOTTOM_LEFT, 60,0);
    lv_obj_set_size(btn_effect, 60, 60);
    lv_obj_set_style_bg_opa(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT); // 透明背景
    lv_obj_set_style_pad_all(btn_effect, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_effect, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_effect, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_effect, photoEffect_Select_event_cb, LV_EVENT_CLICKED, NULL);

    img_effect_s = lv_img_create(btn_effect);
    lv_obj_set_size(img_effect_s, 40, 40);
    lv_obj_align(img_effect_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(img_effect_s, 0, LV_STATE_DEFAULT);
    show_image(img_effect_s, "颜色特效.png");

    time_t now       = time(NULL);
    struct tm *t     = localtime(&now);

    // 时间显示
    Home_Photo_Item->label_datatime = lv_label_create(Home_Photo_Item->photoscr);
    lv_obj_set_pos(Home_Photo_Item->label_datatime, 220, 420);
    lv_label_set_text_fmt(Home_Photo_Item->label_datatime, "%04d/%02d/%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday,t->tm_hour, t->tm_min, t->tm_sec);
    lv_label_set_long_mode(Home_Photo_Item->label_datatime, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(Home_Photo_Item->label_datatime, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(Home_Photo_Item->label_datatime, lv_color_hex(0xFFFFFF),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(Home_Photo_Item->label_datatime, LV_ALIGN_BOTTOM_MID, 0, -12);
    if(getSelect_Index() == TIME_FLAG_OFF) {
        lv_obj_add_flag(Home_Photo_Item->label_datatime, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(Home_Photo_Item->photoscr);


    //相册
    Home_Photo_Item->img_album = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(Home_Photo_Item->img_album, LV_ALIGN_BOTTOM_RIGHT, -80, 0);
    lv_obj_set_size(Home_Photo_Item->img_album, 60, 60);
    lv_obj_set_style_bg_opa(Home_Photo_Item->img_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(Home_Photo_Item->img_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(Home_Photo_Item->img_album, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(Home_Photo_Item->img_album, "photo_album.png");

    // 退出按钮
    Home_Photo_Item->img_exit = lv_button_create(Home_Photo_Item->photoscr);
    lv_obj_align(Home_Photo_Item->img_exit, LV_ALIGN_BOTTOM_RIGHT, -6, 0);
    lv_obj_set_size(Home_Photo_Item->img_exit, 60, 60);
    lv_obj_set_style_bg_opa(Home_Photo_Item->img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(Home_Photo_Item->img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(Home_Photo_Item->img_exit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    show_image(Home_Photo_Item->img_exit, "exit.png");

    // 时间
    label_delay_time_s = lv_label_create(Home_Photo_Item->photoscr);
    lv_label_set_text(label_delay_time_s, "10");
    lv_obj_set_style_text_font(label_delay_time_s, get_usr_fonts(ALI_PUHUITI_FONTPATH, 80),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label_delay_time_s, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label_delay_time_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(label_delay_time_s, LV_OBJ_FLAG_HIDDEN);

    //连续拍照次数文本
    continuous_count_label_s = lv_label_create(ui->page_photo.photoscr);
    lv_obj_add_style(continuous_count_label_s, &ttf_font_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(continuous_count_label_s, lv_color_hex(0x00FF00),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(continuous_count_label_s, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN);

    // 创建缩放等级显示
    create_zoom_bar(ui->page_photo.photoscr);

    /* 设置当前页面按键处理回调 */
    register_all_key();

    // 创建更新定时器
    if(date_timer_s == NULL) {
        date_timer_s = lv_timer_create(photo_var_dynamic_update, 1000, ui);
        lv_timer_ready(date_timer_s);
    }
    events_init_HomePhoto(ui);
}


// 放大按键事件处理
static void zoomin_key_cb(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标
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

// 缩小按键事件处理
static void zoomout_key_cb(void)
{
    restore_icon_on_any_key(); // 任意键恢复图标
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

// 隐藏所有控件并保存状态
void hide_all_widgets(lv_obj_t *parent)
{
    // 如果已经隐藏但还未恢复，则丢弃本次调用
    if (g_widgets_hidden) {
        MLOG_DBG("已隐藏未恢复，丢弃此次隐藏调用\n");
        return;
    }

    // 首先计算需要存储的控件数量
    int child_count = lv_obj_get_child_count(parent);
    // 分配内存存储控件状态
    if (g_widget_states == NULL || g_max_widgets < child_count) {
        g_max_widgets = child_count + 10; // 额外分配一些空间
        g_widget_states = realloc(g_widget_states, g_max_widgets * sizeof(widget_state_t));
        if (g_widget_states == NULL) {
            MLOG_ERR("Failed to allocate memory for widget states\n");
            return;
        }
    }

    g_widget_count = 0;

    // 遍历所有子控件，保存状态并隐藏
    lv_obj_t *child = lv_obj_get_child(parent, 0);
    while (child != NULL) {
        // 保存控件状态
        g_widget_states[g_widget_count].obj = child;
        g_widget_states[g_widget_count].hidden = lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN);

        // 隐藏控件（除非是取景框）
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);

        g_widget_count++;
        child = lv_obj_get_child(parent, g_widget_count);
    }

    MLOG_DBG("Hidden %d widgets\n", g_widget_count);
    g_widgets_hidden = true; // 标记已隐藏
}

// 恢复所有控件的显示状态
void restore_all_widgets(void)
{
    if (g_widget_states == NULL) {
        MLOG_ERR("No widget states to restore\n");
        return;
    }

    for (int i = 0; i < g_widget_count; i++) {
        if (lv_obj_is_valid(g_widget_states[i].obj)) {
            if (g_widget_states[i].hidden) {
                lv_obj_add_flag(g_widget_states[i].obj, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(g_widget_states[i].obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    MLOG_DBG("Restored %d widgets\n", g_widget_count);

    // 清理状态存储
    free(g_widget_states);
    g_widget_states = NULL;
    g_widget_count = 0;
    g_max_widgets = 0;
    g_widgets_hidden = false; // 清除隐藏标记
}

static void wifi_return_to_home_photo(void *user_data)
{
    lv_ui_t *ui = (lv_ui_t *)user_data;
    if(ui == NULL) {
        MLOG_ERR("UI is NULL, using default UI...\n");
        ui = &g_ui;
    }
    ui_load_scr_animation(ui, &ui->page_photo.photoscr, ui->screenHomePhoto_del, NULL, Home_Photo,
                          LV_SCR_LOAD_ANIM_NONE, 0, 0, false, true);
}

static void key_takephoto_power_callback(void)
{
    // 拍照过程中丢弃事件
    // limit_key_flag_s=false 表示延时拍进行中
    // continue_limit_key_flag_s=false 表示连拍进行中
    // continue_take_photo_timer!=NULL 表示连拍定时器运行中
    // is_normal_capturing=true 表示普通拍照进行中
    if (!limit_key_flag_s || !continue_limit_key_flag_s || continue_take_photo_timer != NULL || is_normal_capturing) {
        MLOG_DBG("拍照过程中，丢弃电源键切换图标事件\n");
        return;
    }

    power_key_count = !power_key_count;
    switch (power_key_count) {
    case false:
        restore_all_widgets();
        break;
    case true:
        hide_all_widgets(g_ui.page_photo.photoscr);
        break;
    }
}

void register_all_key(void)
{
    if((get_self_delay_time() != 0 && AIModeSelect_GetMode() == AI_NONE) ||
       ((get_shootmode(1) != 0 && AIModeSelect_GetMode() == AI_NONE))) {
        set_current_page_handler(takephoto_delay_handler);
    } else {
        set_current_page_handler(takephoto_key_handler);
    }
    takephoto_register_callback(key_takephoto_callback);
    takephoto_register_menu_callback(photo_menu_callback);
    takephoto_register_mode_callback(photo_mode_callback);
    takephoto_register_play_callback(photo_play_callback);
    takephoto_register_up_callback(photo_redlight_callback);
    takephoto_register_down_callback(photo_redlight_callback);
    takephoto_register_zoomin_callback(zoomin_key_cb);
    takephoto_register_zoomout_callback(zoomout_key_cb);
    takephoto_register_before_callback(key_takephoto_before_callback);
    takephoto_power_callback(key_takephoto_power_callback);
}

// 更新红光亮级UI显示（拍照和视频模式通用）
void update_redlight_ui(void)
{
    extern bool is_video_mode;
    extern lv_obj_t *g_video_top_controls[6];  // 视频页面顶部控件
    extern void update_video_top_controls_layout(void);  // 视频页面布局更新函数

    MLOG_DBG("update_redlight_ui: brightness_level=%d g_batter_image_index:%d\n", brightness_level,g_batter_image_index);
    if (is_video_mode) {
        // 视频模式：更新视频页面的红光UI
        if (g_video_top_controls[2] && lv_obj_is_valid(g_video_top_controls[2])) {
            if (brightness_level > 0) {
                lv_obj_clear_flag(g_video_top_controls[2], LV_OBJ_FLAG_HIDDEN);
                if (brightness_level > 6) {
                    show_image(g_video_top_controls[2], red_light_image_level[6]);
                } else {
                    show_image(g_video_top_controls[2], red_light_image_level[brightness_level-1]);
                }
                update_video_top_controls_layout();
            } else {
                lv_obj_add_flag(g_video_top_controls[2], LV_OBJ_FLAG_HIDDEN);
                update_video_top_controls_layout();
            }
        }
    } else {
        // 拍照模式：更新拍照页面的红光UI
        lv_ui_t *ui = &g_ui;
        if (ui->page_photo.redlight_level && lv_obj_is_valid(ui->page_photo.redlight_level)) {
            if (brightness_level > 0) {
                lv_obj_clear_flag(ui->page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
                if (brightness_level > 6) {
                    show_image(ui->page_photo.redlight_level, red_light_image_level[6]);
                } else {
                    show_image(ui->page_photo.redlight_level, red_light_image_level[brightness_level-1]);
                }
                update_top_controls_simple();
            } else {
                lv_obj_add_flag(ui->page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
                update_top_controls_simple();
            }
        }
    }
}

// ========== 图标选择弹窗回调实现 ==========

// 分辨率选择项回调（内部）
static void icon_select_res_on_select(uint32_t index, void *user_data)
{
    PARAM_MENU_S menu_param = {0};
    PARAM_GetMenuParam(&menu_param);
    
    // 安全检查
    if (menu_param.PhotoSize.ItemCnt == 0 || menu_param.PhotoSize.Items == NULL) {
        return;
    }
    if (index >= menu_param.PhotoSize.ItemCnt) {
        return;
    }
    
    // 检查标签是否有效
    const char* label = menu_param.PhotoSize.Items[index].Desc;
    if (label == NULL) {
        label = "Unknown";
    }
    
    // 发送设置消息
    MESSAGE_S event = {0};
    event.topic = EVENT_MODEMNG_SETTING;
    event.arg1 = PARAM_MENU_PHOTO_SIZE;
    event.arg2 = index;
    MODEMNG_SendMessage(&event);
    
    // 更新UI
    photo_setRes_Index(index);
    photo_setRes_Label(label);
    
    // 检查 g_top_controls[0] 是否有效
    if (g_top_controls[0] != NULL && lv_obj_is_valid(g_top_controls[0])) {
        show_image(g_top_controls[0], photo_getRes_Icon());
    }
    
    // 复位缩放
    set_zoom_level(1);
    
    MLOG_INFO("Resolution changed to: %s\n", label);
}

// 分辨率选择弹窗事件
static void icon_select_res_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    // 检查父对象是否有效
    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
        return;
    }
    
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }

    PARAM_MENU_S menu_param = {0};
    PARAM_GetMenuParam(&menu_param);
    
    // 检查菜单项是否有效
    if (menu_param.PhotoSize.ItemCnt == 0 || menu_param.PhotoSize.Items == NULL) {
        return;
    }
    
    // 构建分辨率选项
    static icon_select_item_t res_items[PARAM_MENU_ITEM_MAX];
    uint32_t item_count = 0;
    
    for (uint32_t i = 0; i < menu_param.PhotoSize.ItemCnt && i < PARAM_MENU_ITEM_MAX; i++) {
        int width = menu_param.PhotoSize.Items[i].Value;
        int icon_idx = get_photo_res_icon_index_by_width(width);
        res_items[i].icon = photo_getRes_IconByIndex(icon_idx >= 0 ? icon_idx : 0);
        res_items[i].label = (menu_param.PhotoSize.Items[i].Desc != NULL) ? 
                             menu_param.PhotoSize.Items[i].Desc : "Unknown";
        item_count++;
    }
    
    if (item_count == 0) return;
    
    // 创建弹窗
    create_icon_select_popup(g_ui.page_photo.photoscr, ICON_SELECT_RESOLUTION,
                            res_items, item_count,
                            photo_getRes_Index(),
                            icon_select_res_on_select, NULL);
}

// 红外灯亮度选择项回调（内部）
static void icon_select_redlight_on_select(uint32_t index, void *user_data)
{
    if (index >= 8) {  // 红光有8个选项(0-7)，0为关闭
        return;
    }
    
    // 如果要开启红外灯（亮度>0），检查电量
    if (index > 0) {
        // 检查是否低电量（电池图标索引为1表示低电量0%-25%）
        if (g_batter_image_index == 1) {
            MLOG_ERR("电量过低，无法开启红外灯\n");
            return;
        }
    }
    
    // 更新红光等级 (0-7)
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
    if (brightness_level > 0) {
        lv_obj_clear_flag(g_ui.page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
        if (brightness_level > 6) {
            show_image(g_ui.page_photo.redlight_level, red_light_image_level[6]);
        } else {
            show_image(g_ui.page_photo.redlight_level, red_light_image_level[brightness_level - 1]);
        }
        update_top_controls_simple();
    } else {
        lv_obj_add_flag(g_ui.page_photo.redlight_level, LV_OBJ_FLAG_HIDDEN);
        update_top_controls_simple();
    }
    
    MLOG_INFO("拍照模式红光等级设置为: %d\n", brightness_level);
}

// 红外灯亮度选择弹窗事件
static void icon_select_redlight_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    // 检查父对象是否有效
    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
        return;
    }
    
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
    create_icon_select_popup(g_ui.page_photo.photoscr, ICON_SELECT_REDLIGHT,
                            redlight_items, item_count,
                            selected_index,
                            icon_select_redlight_on_select, NULL);

    MLOG_DBG("拍照模式红外灯弹窗：最大档位=%d，选项数量=%d，当前选中=%d\n",
             max_level, item_count, selected_index);
}

// 屏幕亮度选择项回调（内部）
static void icon_select_brightness_on_select(uint32_t index, void *user_data)
{
    if (index >= BRIGHTNESS_LEVEL_COUNT) {
        return;
    }
    
    // 更新亮度
    setsysMenu_brightness_Index(index);
    brightness_set_level(index + 1);
    
    // 更新UI
    char* brightness_buf[] = {"1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png"};
    show_image(g_top_controls[2], brightness_buf[index]);
    
    MLOG_INFO("Brightness level changed to: %d\n", index + 1);
}

// 屏幕亮度选择弹窗事件
static void icon_select_brightness_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    // 检查父对象是否有效
    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
        return;
    }
    
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }

    // 构建屏幕亮度选项
    static icon_select_item_t brightness_items[BRIGHTNESS_LEVEL_COUNT];
    char* brightness_buf[] = {"1.png", "2.png", "3.png", "4.png", "5.png", "6.png", "7.png"};
    char brightness_labels[BRIGHTNESS_LEVEL_COUNT][32];
    
    for (int i = 0; i < BRIGHTNESS_LEVEL_COUNT; i++) {
        brightness_items[i].icon = brightness_buf[i];
        snprintf(brightness_labels[i], sizeof(brightness_labels[i]), "Level %d", i + 1);
        brightness_items[i].label = brightness_labels[i];
    }
    
    // 创建弹窗
    create_icon_select_popup(g_ui.page_photo.photoscr, ICON_SELECT_BRIGHTNESS,
                            brightness_items, BRIGHTNESS_LEVEL_COUNT,
                            get_curr_brightness(),
                            icon_select_brightness_on_select, NULL);
}

// 连拍模式选择项回调（内部）
static void icon_select_shootmode_on_select(uint32_t index, void *user_data)
{
    extern void set_shootmode(uint8_t mode);
    
    // 更新连拍模式
    set_shootmode(index);
    
    // 更新UI
    char* continue_buf[] = {"连拍关闭.png", "连拍3.png", "连拍5.png", "连拍7.png"};
    if (index == 0) {
        lv_obj_add_flag(g_top_controls[3], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(g_top_controls[3], LV_OBJ_FLAG_HIDDEN);
        show_image(g_top_controls[3], continue_buf[index]);
    }
    
    MLOG_INFO("Shoot mode changed to: %d\n", index);
}

// 连拍模式选择弹窗事件
static void icon_select_shootmode_callback(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;
    
    // 检查父对象是否有效
    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
        return;
    }
    
    if (is_icon_select_popup_exists()) {
        delete_icon_select_popup();
        return;
    }
    
    // 构建连拍模式选项
    static icon_select_item_t shootmode_items[4];
    char* continue_buf[] = {"连拍关闭.png", "连拍3.png", "连拍5.png", "连拍7.png"};
    char shootmode_labels[4][32] = {"关闭", "3张", "5张", "7张"};
    
    for (int i = 0; i < 4; i++) {
        shootmode_items[i].icon = continue_buf[i];
        shootmode_items[i].label = shootmode_labels[i];
    }
    
    // 创建弹窗
    create_icon_select_popup(g_ui.page_photo.photoscr, ICON_SELECT_SHOOTMODE,
                            shootmode_items, 4,
                            get_shootmode(0),
                            icon_select_shootmode_on_select, NULL);
}

// 渐隐动画完成回调
void photoanimCompleted_objDel_cb(lv_anim_t *a)
{
    // 移除标志
    lv_timer_resume(date_timer_s);
    delete_all_handle();
}

// 特效选择回调
static void photoEffect_Select_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    MLOG_DBG("event: %s\n", lv_event_code_get_name(code));
    switch(code) {
        case LV_EVENT_CLICKED: {
            if(get_is_effect_exist() == true) {
                delete_all_handle();
                // 移除标志
                lv_timer_resume(date_timer_s);
            } else {
                lv_timer_pause(date_timer_s);
                // 创建滚动列表
                float_effect_creat(img_effect_s, g_ui.page_photo.photoscr);
                // 创建控件并启动渐渐隐藏动画
                create_gradually_hide_anim(photoanimCompleted_objDel_cb,8000);
            }
            // Update current screen layout.
            lv_obj_update_layout(g_ui.page_photo.photoscr);
        } break;
        default: break;
    }
}

static void continue_take_photo_timer_cb(lv_timer_t* timer)
{
    static uint8_t take_photo_num = 0;
    if (get_shootmode(1) - take_photo_num != 0) // 没有拍照完成
    {
        CVI_VO_PauseChn(0, 0);
        MESSAGE_S Msg = { 0 };
        Msg.topic = EVENT_MODEMNG_START_PIV;
        MODEMNG_SendMessage(&Msg);
        ui_common_wait_piv_end();
        CVI_VO_ResumeChn(0, 0);
        MLOG_DBG("第%d次拍照\n", take_photo_num+1);
        lv_label_set_text_fmt(continuous_count_label_s, "%d/%d",take_photo_num + 1, get_shootmode(1));
        take_photo_num++;
    } else {
        restore_all_widgets();
        lv_timer_resume(date_timer_s);

        // 隐藏连续拍照次数
        if (!lv_obj_has_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(continuous_count_label_s, LV_OBJ_FLAG_HIDDEN);
        }
        enable_touch_events(); // 连拍结束，恢复触摸
        enable_hardware_input_device(0);
        enable_hardware_input_device(1);
        continue_limit_key_flag_s = true;
        is_normal_capturing = false; // 清除普通拍照标志
        take_photo_num = 0;
        lv_timer_del(continue_take_photo_timer);
        continue_take_photo_timer = NULL;
    }
}

void continue_take_photo(void)
{
    if(continue_take_photo_timer == NULL) {
        continue_take_photo_timer = lv_timer_create(continue_take_photo_timer_cb, 100, NULL);
        lv_timer_ready(continue_take_photo_timer);
    }
}


// 长按定时器回调函数
static void zoom_longpress_timer_cb(lv_timer_t *timer)
{

    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
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
    
    if (g_ui.page_photo.photoscr == NULL || !lv_obj_is_valid(g_ui.page_photo.photoscr)) {
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

// ========== 实时动物检测功能实现 ==========

// 用于 lv_async_call 的数据结构
typedef struct {
    CVI_U32 count;
    ANIP_RESULT_S results[MAX_ANIMAL_BOXES];  // 识别结果（名称）
} anip_result_data_t;

/* 动物检测框绘制回调函数 - 供anip_service调用
 * 识别框只在OSD层绘制，UI层只显示标签
 */
static int anip_draw_rects_callback(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects)
{
    /* 直接调用 MEDIA_DrawRects 绘制矩形框（OSD层） */
    CVI_S32 ret = MEDIA_DrawRects(osd_id, num, rects);
    if (ret != 0) {
        MLOG_ERR("[ANIP] MEDIA_DrawRects failed: %d\n", ret);
    }
    return ret;
}

/* 动物识别结果回调函数 - 供anip_service调用
 * 返回动物名称，用于在UI层显示标签
 * 注意：框由OSD层绘制，标签显示在UI层顶部区域
 */
static void anip_result_callback(CVI_U32 osd_id, ANIP_RESULT_S* results, CVI_U32 count)
{
    (void)osd_id;

    /* 检查是否启用 */
    if (!s_anip_enabled || count == 0 || results == NULL) {
        return;
    }

    /* 将结果数据复制到临时结构，通过 lv_async_call 发送到主线程 */
    static anip_result_data_t result_data;
    result_data.count = (count > MAX_ANIMAL_BOXES) ? MAX_ANIMAL_BOXES : count;
    for (CVI_U32 i = 0; i < result_data.count; i++) {
        result_data.results[i] = results[i];
    }

    /* 使用 lv_async_call 调度到主线程执行UI操作 */
    lv_async_call(anip_result_ui_update, &result_data);
}

/* 在主线程中更新动物名称标签UI
 * 通过 ANIP_SERVICE_Get_Rects 获取框位置，将标签显示在对应框的上方
 */
static void anip_result_ui_update(void *user_data)
{
    anip_result_data_t *result_data = (anip_result_data_t *)user_data;
    CVI_U32 count = result_data->count;

    /* 检查画布有效性 */
    if (s_ani_canvas == NULL || !lv_obj_is_valid(s_ani_canvas)) {
        return;
    }

    /* 从 anip_service 获取最新的框位置信息 */
    RECT_S rects[MAX_ANIMAL_BOXES] = {0};
    CVI_U32 rect_count = ANIP_SERVICE_Get_Rects(rects, MAX_ANIMAL_BOXES);

    /* 清除所有旧的标签 */
    for (int i = 0; i < MAX_ANIMAL_BOXES; i++) {
        if (s_ani_boxes[i].label != NULL && lv_obj_is_valid(s_ani_boxes[i].label)) {
            lv_obj_add_flag(s_ani_boxes[i].label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 在对应检测框上方显示动物名称 */
    CVI_U32 show_count = (count < rect_count) ? count : rect_count;
    if (show_count > MAX_ANIMAL_BOXES) show_count = MAX_ANIMAL_BOXES;

    for (CVI_U32 i = 0; i < show_count; i++) {
        if (strlen(result_data->results[i].name) == 0) {
            continue;
        }

        /* 只在动物类别(cls_idx==0)时才显示标签，人和车辆不显示 */
        if (result_data->results[i].cls_idx != 0) {
            continue;
        }

        RECT_S* rect = &rects[i];

        /* 跳过无效的框 */
        if (rect->u32Width <= 0 || rect->u32Height <= 0) {
            continue;
        }

        animal_box_t* box = &s_ani_boxes[i];

        /* 创建或更新标签 */
        if (box->label == NULL || !lv_obj_is_valid(box->label)) {
            box->label = lv_label_create(s_ani_canvas);
            if (box->label == NULL) {
                continue;
            }
            lv_obj_set_style_bg_color(box->label, ANI_LABEL_BG_COLOR, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(box->label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(box->label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(box->label, get_usr_fonts(ALI_PUHUITI_FONTPATH, 16), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(box->label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(box->label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
        }

        /* 设置标签文本 */
        char label_text[64];
        snprintf(label_text, sizeof(label_text), "%s", result_data->results[i].name);
        lv_label_set_text(box->label, label_text);

        /* 定位标签在框的左上角上方 */
        CVI_S32 label_x = rect->s32X + 4;  /* 框左边距+4像素偏移 */
        CVI_S32 label_y = rect->s32Y - 24; /* 框顶部向上偏移24像素 */

        /* 边界检查，防止标签超出屏幕顶部 */
        if (label_y < 0) label_y = rect->s32Y + 4;

        lv_obj_set_pos(box->label, label_x, label_y);
        lv_obj_clear_flag(box->label, LV_OBJ_FLAG_HIDDEN);

        // MLOG_INFO("[ANIP] Label %d: %s at (%d, %d) on rect (%d,%d %dx%d)\n",
        //           i, result_data->results[i].name, label_x, label_y,
        //           rect->s32X, rect->s32Y, rect->u32Width, rect->u32Height);
    }
}

/* 创建动物检测覆盖层 */
static void create_anip_overlay(lv_obj_t *parent)
{
    if (parent == NULL || !lv_obj_is_valid(parent)) {
        MLOG_ERR("[ANIP] Parent object is invalid!\n");
        return;
    }

    /* 如果已存在，先销毁 */
    destroy_anip_overlay();

    /* 创建透明容器用于承载检测框 */
    s_ani_canvas = lv_obj_create(parent);
    if (s_ani_canvas == NULL) {
        MLOG_ERR("[ANIP] Failed to create overlay container!\n");
        return;
    }

    /* 设置为透明，不阻挡点击事件 */
    lv_obj_set_size(s_ani_canvas, H_RES, V_RES);
    lv_obj_set_pos(s_ani_canvas, 0, 0);
    lv_obj_set_style_bg_opa(s_ani_canvas, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_ani_canvas, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_ani_canvas, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 不拦截点击事件，让事件穿透到下面的控件 */
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(s_ani_canvas, LV_OBJ_FLAG_SCROLL_MOMENTUM);

    /* 确保在最上层 */
    lv_obj_move_to_index(s_ani_canvas, -1);

    MLOG_INFO("[ANIP] Animal detection overlay created\n");
}

/* 销毁动物检测覆盖层 */
static void destroy_anip_overlay(void)
{
    if (s_ani_canvas != NULL) {
        if (lv_obj_is_valid(s_ani_canvas)) {
            lv_obj_del(s_ani_canvas);
        }
        s_ani_canvas = NULL;
    }

    /* 销毁所有标签对象 */
    for (int i = 0; i < MAX_ANIMAL_BOXES; i++) {
        if (s_ani_boxes[i].label != NULL) {
            if (lv_obj_is_valid(s_ani_boxes[i].label)) {
                lv_obj_del(s_ani_boxes[i].label);
            }
            s_ani_boxes[i].label = NULL;
        }
        s_ani_boxes[i].valid = false;
    }

    MLOG_INFO("[ANIP] Animal detection overlay destroyed\n");
}

/* 启动实时动物检测服务（带重试机制，等待VPROC初始化完成） */
static int start_anip_service(void)
{
    /* 检查是否已经启动 */
    if (g_anip_handle >= 0) {
        MLOG_INFO("[ANIP] Service already started, handle=%d\n", g_anip_handle);
        return 0;
    }

    MLOG_INFO("[ANIP] start_anip_service called\n");

    /* 配置服务参数 - 使用 vproc1 (grp=1，已绑定到VI) */
    ANIP_SERVICE_PARAM_S param = {0};
    param.in_vpss_grp = 1;  /* VPSS Group 1 - 与vproc1配置一致 */
    param.in_vpss_chn = 0;  /* VPSS通道0 */
    param.in_width = 640;   /* 根据实际配置调整 */
    param.in_height = 480;
    param.osd_mirror = 0;
    param.sensitivity = 30;
    param.max_results = MAX_ANIMAL_BOXES;
    param.det_enable = 1;
    param.rec_enable = 1;

    MLOG_INFO("[ANIP] Using VPSS grp=%d, chn=%d\n", param.in_vpss_grp, param.in_vpss_chn);

    /* 配置 osd_id = 1 对应 config_media_cam0_photo_*.ini 中的 osd_content1 (type=4 OBJECT类型)
     * 用于绘制动物检测框，与人脸识别使用相同的OSD区域绘制机制 */
    param.osd_id = 1;

    /* 注册回调函数 */
    ANIP_SERVICE_Register_DrawRects_Callback(anip_draw_rects_callback);
    ANIP_SERVICE_Register_Result_Callback(anip_result_callback);

    /* 创建服务 */
    CVI_S32 ret = ANIP_SERVICE_Create(&g_anip_handle, &param);
    if (ret != 0) {
        MLOG_ERR("[ANIP] ANIP_SERVICE_Create failed: %d\n", ret);
        ANIP_SERVICE_Unregister_DrawRects_Callback();
        ANIP_SERVICE_Unregister_Result_Callback();
        g_anip_handle = -1;
        return -1;
    }

    s_anip_enabled = true;
    MLOG_INFO("[ANIP] Real-time animal detection service started, handle=%d\n", g_anip_handle);
    return 0;
}

/* 停止实时动物检测服务 */
static void stop_anip_service(void)
{
    /* 先禁用，防止新的回调触发 */
    s_anip_enabled = false;

    /* 销毁服务（这会停止任务线程并等待其结束） */
    if (g_anip_handle >= 0) {
        /* 先清除OSD层的框 */
        ANIP_SERVICE_Clear_Rects(g_anip_handle);
        ANIP_SERVICE_Destroy(g_anip_handle);
        g_anip_handle = -1;
    }

    /* 注销回调（在销毁服务之后） */
    ANIP_SERVICE_Unregister_DrawRects_Callback();
    ANIP_SERVICE_Unregister_Result_Callback();

    MLOG_INFO("[ANIP] Real-time animal detection service stopped\n");
}
