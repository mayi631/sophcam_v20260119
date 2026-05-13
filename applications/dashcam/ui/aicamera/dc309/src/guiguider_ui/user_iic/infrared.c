#define DEBUG
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <linux/i2c.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "mlog.h"
#include "hal_pwm.h"  // 添加PWM头文件
#include "ui_common.h"
#include "page_all.h"

#define I2C_DEVICE  "/dev/i2c-%d"
#define TP_I2C_ADDR 0x30 //设备地址
int bus_id = 0;		// I2C bus id

// PWM相关定义
#define LED_PWM_GROUP 1      // PWM组号
#define LED_PWM_CHANNEL 2    // PWM8通道
#define LED_MAX_BRIGHTNESS 100  // 最大亮度值

int red_light_level[] = {0, 64, 74, 80, 84, 90, 96, 100};
// 外部变量声明：电池等级索引
extern int32_t g_batter_image_index;
// 根据电池电量获取红外灯最大允许档位
int8_t get_max_red_light_level(void)
{
    // g_batter_image_index 映射关系：
    // 0: 充电
    // 1: 电量在0%-25% (1/3格)
    // 2: 电量在25%-70% (2/3格)
    // 3: 电量在70%以上 (满电)
    switch (g_batter_image_index) {
        case 1: // 0%-25% 电量，最大只能开到3档
            return 3;
        case 2: // 25%-70% 电量，最大只能开到5档
        case 3:
            return 5;
        case 0: // 充电状态
        case 4: // 满电
        default:
            return 7; // 最大7档
    }
}

// PWM属性结构体
static HAL_PWM_S led_pwm_attr = {0};
static bool pwm_initialized = false;
int8_t brightness_level = 0;
/**
 * @brief 初始化PWM用于LED亮度控制
 * 
 * @return int 成功返回0，失败返回-1
 */
static int init_led_pwm(void)
{
    if (pwm_initialized) {
        return 0;  // 已经初始化过
    }
    
    // 配置PWM参数
    led_pwm_attr.group = LED_PWM_GROUP;
    led_pwm_attr.channel = LED_PWM_CHANNEL;
    led_pwm_attr.period = 100;        // 周期（单位：纳秒）
    led_pwm_attr.duty_cycle = 0;      // 初始占空比为0（LED熄灭）
    
    // 初始化PWM
    if (HAL_PWM_Init(led_pwm_attr) != 0) {
        MLOG_ERR("LED PWM初始化失败\n");
        return -1;
    }
    
    pwm_initialized = true;
    MLOG_DBG("LED PWM初始化成功，使用PWM%d\n", LED_PWM_CHANNEL);
    return 0;
}

/**
 * @brief 设置LED亮度
 * 
 * @param brightness 亮度值 (0-100)，0为完全关闭，100为最亮
 * @return int 成功返回0，失败返回-1
 */
static int set_led_brightness(int brightness)
{
    // 确保亮度值在有效范围内
    if (brightness < 0) brightness = 0;
    if (brightness > LED_MAX_BRIGHTNESS) brightness = LED_MAX_BRIGHTNESS;
    
    // 如果PWM未初始化，先初始化
    if (!pwm_initialized) {
        if (init_led_pwm() != 0) {
            MLOG_DBG("PWM未初始化\n");
            return -1;
        }
    }
    
    // 设置PWM占空比
    led_pwm_attr.duty_cycle = brightness;
    // MLOG_DBG("BUG调试 %d \n",led_pwm_attr.channel);
    if (HAL_PWM_Set_Param(led_pwm_attr) != 0) {
        MLOG_ERR("设置LED亮度失败: %d%%\n", brightness);
        return -1;
    }
    
    MLOG_DBG("LED亮度设置为: %d%%\n", brightness);
    return 0;
}

/**
 * @brief 执行I2C设备测试函数
 * 
 * 打开指定的I2C总线，设置从设备地址，并向寄存器0x02写入值0x01。
 * 该函数主要用于测试I2C设备的通信功能，目前只实现写入操作。
 * 写入完成后会切换value的值用于下一次测试。
 * 
 * @return int 成功返回0，失败返回错误码：
 *         -ENODEV: 无法打开I2C设备或无法访问从设备
 *         -1: I2C写入操作失败
 */
int user_i2c0(uint8_t reg, uint8_t value)
{
    int i2c_file;
    char bus_path[64];
    MLOG_DBG("I2C测试\n");
    
    // 打开I2C设备
    snprintf(bus_path, sizeof(bus_path), I2C_DEVICE, bus_id);
    i2c_file = open(bus_path, O_RDWR);
    if(i2c_file < 0) {
        MLOG_ERR("Failed to open the I2C bus: %s\n", strerror(errno));
        return -ENODEV;
    }

    // 设置I2C从设备地址
    if(ioctl(i2c_file, I2C_SLAVE, TP_I2C_ADDR) < 0) {
        MLOG_ERR("Failed to acquire bus access and/or talk to slave: %s\n", strerror(errno));
        close(i2c_file);
        return -ENODEV;
    }

    // 写I2C
    uint8_t buf[2] = {reg, value};

    if(write(i2c_file, buf, sizeof(buf)) != sizeof(buf)) {
        perror("Write failed");
        close(i2c_file);
        return -1;
    }

    return 0;
}

uint8_t g_curreffectindex = 0;
bool statusLight_is_on = 0;
/**
 * 打开LED灯（使用PWM控制亮度）
 * 
 * 通过PWM控制LED灯亮度，默认设置为最大亮度
 */
void led_on(void)
{
    // 如果PWM控制失败，可以回退到原来的I2C控制方式
    user_i2c0(0x01, 0x01);
    extern bool is_video_mode;
    //开启黑白特效
    g_curreffectindex = geteffect_index();    
    MESSAGE_S event = {0};
    event.topic     = EVENT_MODEMNG_SETTING;
    if(is_video_mode == false) {
        event.arg1 = PARAM_MENU_PHOTO_EFFECT;
    } else if(is_video_mode == true) {
        event.arg1 = PARAM_MENU_VIDEO_EFFECT;
    }
    event.arg2 = 8;
    MODEMNG_SendMessage(&event);
}

/**
 * 打开LED灯并设置指定亮度
 *
 * @param levlel 亮度值 (0-7)
 */
void led_on_with_brightness(int levlel)
{
    if (levlel >= 7)
        levlel = 7;
    else if (levlel <= 0)
        levlel = 0;

    // 如果是要关闭红外灯（亮度为0），直接执行
    if (levlel == 0) {
        brightness_level = 0;
        MLOG_DBG("红外灯已关闭\n");
        return;
    }

    // 检查是否空格电量（电池图标索引为1表示低电量0%-25%）
    if (g_batter_image_index == 1) {
        MLOG_ERR("电量过低，无法开启红外灯\n");
        led_off();  // 关闭红外灯
        brightness_level = 0;
        // TODO: 可以在这里添加提示用户电量低的逻辑
        return;
    }

    // 根据电池电量限制最大亮度档位
    int8_t max_level = get_max_red_light_level();
    if (levlel > max_level) {
        levlel = max_level;
        MLOG_DBG("电池电量限制，红外灯亮度自动降档至 %d\n", levlel);
    }

    if (set_led_brightness(red_light_level[levlel]) == 0) {
        brightness_level = levlel;
        MLOG_DBG("LED灯已打开，自定义亮度等级: %d\n", levlel);
    } else {
        MLOG_ERR("设置等级%d(%d)失败，使用默认亮度等级\n",red_light_level[levlel],levlel);
        led_on();  // 回退到默认亮度
    }
}

/**
 * 关闭LED灯
 * 
 * 通过PWM控制关闭LED灯，将亮度设置为0
 */
void led_off(void)
{
    user_i2c0(0x01, 0x00);
    led_cleanup();

    //恢复原有特效
    extern bool is_video_mode;
    MESSAGE_S event = {0};
    event.topic     = EVENT_MODEMNG_SETTING;
    if(is_video_mode == false) {
        event.arg1 = PARAM_MENU_PHOTO_EFFECT;
    } else if(is_video_mode == true) {
        event.arg1 = PARAM_MENU_VIDEO_EFFECT;
    }
    event.arg2 = g_curreffectindex;
    MODEMNG_SendMessage(&event);
}

/**
 * 清理PWM资源
 */
void led_cleanup(void)
{
    if (pwm_initialized) {
        HAL_PWM_Deinit(led_pwm_attr);
        pwm_initialized = false;
        MLOG_DBG("LED PWM资源已清理\n");
    }
}

/**
 * 打开红外截止滤镜
 * 
 * 通过I2C通信向设备发送控制指令，激活红外截止滤镜功能
 * 用于在夜间或低光环境下切换摄像头的工作模式
 */
void ircut_on(void)
{
    user_i2c0(0x02, 0x01);
}

/**
 * 关闭红外截止滤镜
 * 
 * 通过I2C通信向设备发送控制指令，关闭红外截止滤镜功能
 * 用于在正常光照环境下切换摄像头的工作模式
 */
void ircut_off(void)
{
    user_i2c0(0x01, 0x00);
}

/**
 * 根据电池电量自动调整红外灯亮度档位
 * 当电量下降时，自动降档到当前电量允许的最大档位
 */
void auto_adjust_redlight_by_battery(void)
{
    extern void update_redlight_ui(void);  // UI更新函数声明

    if (brightness_level == 0) {
        // 红外灯未开启，无需调整
        return;
    }

    // 检查是否空格电量（电池图标索引为1表示低电量0%-10%）
    if (g_batter_image_index == 1) {
        MLOG_ERR("电量过低，强制关闭红外灯\n");
        led_off();  // 强制关闭红外灯
        ircut_off();
        brightness_level = 0;  // 将亮度等级设置为0，确保UI能正确更新
        // 更新UI显示
        update_redlight_ui();
        return;
    }

    // 获取当前电量允许的最大档位
    int8_t max_level = get_max_red_light_level();

    // 如果当前亮度超过允许的最大档位，则自动降档
    if (brightness_level > max_level) {
        MLOG_INFO("电池电量下降，红外灯从 %d 档自动降至 %d 档\n", brightness_level, max_level);
        if (set_led_brightness(red_light_level[max_level]) == 0) {
            brightness_level = max_level;
            // 更新UI显示
            update_redlight_ui();
        }
    }
}