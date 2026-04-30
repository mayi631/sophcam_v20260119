#define DEBUG
#include "ui_common.h"
#include "mlog.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>
#ifdef SERVICES_LIVEVIEW_ON
#include "volmng.h"
#endif
#include "page_all.h"
#include <linux/input.h>

//#include "event_recorder_player.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

static UI_MESSAGE_CONTEXT s_stMessageCtx = {
    .bMsgProcessed = true,
    .MsgMutex = PTHREAD_MUTEX_INITIALIZER,
};
uint32_t ui_event_type;
static bool key_power_off = false;
int32_t PowerButton_Event(void);
char pic_filepath[128] = "";
static uint32_t g_is_mute_btn_voice = 0;

// 同步消息相关变量
static pthread_cond_t g_sync_cond = PTHREAD_COND_INITIALIZER;
static int32_t g_sync_result = -1;
static bool g_sync_done = false;

#ifndef SERVICES_LIVEVIEW_ON
#define VOICE_MAX_SEGMENT_CNT (5)
typedef struct _VOICEPLAY_VOICE_S {
    uint32_t volume;
    uint32_t u32VoiceCnt;
    uint32_t au32VoiceIdx[VOICE_MAX_SEGMENT_CNT];
    bool bDroppable;
} VOICEPLAY_VOICE_S;
#endif

static int32_t UICOMM_MessageResult(EVENT_S* pstEvent)
{
    int32_t s32Ret = 0;

    // 注意：这里不获取锁，因为同步等待时已经持有锁
    // 需要外部保证线程安全
    if (!s_stMessageCtx.bMsgProcessed) {
        CVI_LOGD("event(%x)\n\n", pstEvent->topic);
        if ((s_stMessageCtx.stMsg.topic == pstEvent->topic)
            && (s_stMessageCtx.stMsg.arg1 == pstEvent->arg1)
            && (s_stMessageCtx.stMsg.arg2 == pstEvent->arg2)) {
            if (s_stMessageCtx.pfnMsgResultProc != NULL) {
                s32Ret = s_stMessageCtx.pfnMsgResultProc(pstEvent);
                if (0 != s32Ret) {
                    CVI_LOGE("pfnMsgResultProc() Error:%#x\n", s32Ret);
                }
            }
            s_stMessageCtx.bMsgProcessed = true;
        }
    }

    return s32Ret;
}

bool ui_common_cardstatus(void)
{
    switch (MODEMNG_GetCardState()) {
    case CARD_STATE_REMOVE:
        ui_event_type = EVT_NO_SDCARD;
        break;
    case CARD_STATE_AVAILABLE:
        return true;
        break;
    case CARD_STATE_ERROR:
        ui_event_type = EVT_SDCARD_ERROR;
        break;
    case CARD_STATE_FSERROR:
    case CARD_STATE_UNAVAILABLE:
        ui_event_type = EVT_SDCARD_NEED_FORMAT;
        break;
    case CARD_STATE_SLOW:
        ui_event_type = EVT_SDCARD_SLOW;
        break;
    case CARD_STATE_CHECKING:
        ui_event_type = EVT_SDCARD_CHECKING;
        break;
    case CARD_STATE_READ_ONLY:
        ui_event_type = EVT_SDCARD_READ_ONLY;
        break;
    case CARD_STATE_MOUNT_FAILED:
        ui_event_type = EVT_SDCARD_MOUNT_FAILED;
        break;
    case CARD_STATE_FULL_SPACE:
        ui_event_type = EVT_SDCARD_SPACE_FULL;
        return true;
        break;
    default:
        CVI_LOGE("value is invalid\n");
        break;
    }
    return false;
}

static bool ui_close_flag = false;
int32_t PowerButton_Event(void)
{
    MESSAGE_S Msg = { 0 };
    u_int32_t s32Ret = 0;
    if (key_power_off == true) {
        Msg.topic = EVENT_MODEMNG_POWEROFF;
        s32Ret = MODEMNG_SendMessage(&Msg);
        if (0 != s32Ret) {
            CVI_LOGI("MODEMNG_SendMessage fail\n");
            return -1;
        }
    } else {
        if (ui_close_flag == false) {
            CVI_LOGE("pannel is going close\n");
            ui_close_flag = true;
        } else {
            CVI_LOGE("pannel is going open\n");
            ui_close_flag = false;
        }
    }
    return 0;
}

static pthread_mutex_t g_piv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_piv_cond = PTHREAD_COND_INITIALIZER;
static bool g_piv_end_flag = false;

// 提供等待和通知的接口
void ui_common_wait_piv_end(void)
{
    pthread_mutex_lock(&g_piv_mutex);
    while (!g_piv_end_flag) {
        pthread_cond_wait(&g_piv_cond, &g_piv_mutex);
    }
    g_piv_end_flag = false; // 重置标志位
    pthread_mutex_unlock(&g_piv_mutex);
}

void ui_common_notify_piv_end(void)
{
    pthread_mutex_lock(&g_piv_mutex);
    g_piv_end_flag = true;
    pthread_cond_signal(&g_piv_cond);
    pthread_mutex_unlock(&g_piv_mutex);
}

int32_t ui_common_eventcb(void* argv, EVENT_S* msg)
{
    MESSAGE_S Msg = { 0 };
    int32_t s32Ret = 0;
    /*receive message result*/
    s32Ret = UICOMM_MessageResult(msg);
    APPCOMM_CHECK_RETURN_WITH_ERRINFO(s32Ret, s32Ret, "MessageResult");

    MLOG_INFO("receive topic: %s\n", event_topic_get_name(msg->topic));
    switch (msg->topic) {
    case EVENT_MODEMNG_CARD_REMOVE: {
        if (MODEMNG_GetCurWorkMode() == WORK_MODE_PLAYBACK) {
            Msg.topic = EVENT_MODEMNG_MODESWITCH;
            Msg.arg1 = WORK_MODE_MOVIE;
            MODEMNG_SendMessage(&Msg);
        } else {
            ui_common_cardstatus();
        }
    } break;
    // case EVENT_MODEMNG_CARD_AVAILABLE:
    case EVENT_MODEMNG_RESET: {
        uint32_t u32ModeState = 0;
        MODEMNG_GetModeState(&u32ModeState);
        CVI_LOGD("u32ModeState == %d\n", u32ModeState);
        if (ui_common_cardstatus() == true && u32ModeState != MEDIA_MOVIE_STATE_MENU) {
            Msg.topic = EVENT_MODEMNG_START_REC;
            MODEMNG_SendMessage(&Msg);
        }
    } break;
    case EVENT_MODEMNG_CARD_FSERROR:
    case EVENT_MODEMNG_CARD_SLOW:
    case EVENT_MODEMNG_CARD_CHECKING:
    case EVENT_MODEMNG_CARD_UNAVAILABLE:
    case EVENT_MODEMNG_CARD_ERROR:
    case EVENT_MODEMNG_CARD_READ_ONLY:
    case EVENT_MODEMNG_CARD_MOUNT_FAILED:
        ui_common_cardstatus();
        break;
    case EVENT_MODEMNG_MODEOPEN: {
        completed_execution_and_unregister_cb();
        CVI_LOGD("EVENT_MODEMNG_MODEOPEN arg1:%d\n", msg->arg1);
        switch (msg->arg1) {
        case WORK_MODE_MOVIE:
            break;
        case WORK_MODE_PLAYBACK:
            break;
        case WORK_MODE_USBCAM:
            break;
        case WORK_MODE_USB:
            break;
        case WORK_MODE_UVC:
            break;
        case WORK_MODE_STORAGE:
            break;
        case WORK_MODE_BOOT:
            set_exit_completed(true);
        default:
            break;
        }
        break;
    }
    case EVENT_MODEMNG_MODECLOSE: {
        CVI_LOGD("EVENT_MODEMNG_MODECLOSE\n");
        switch (msg->arg1) {
        case WORK_MODE_MOVIE:
            break;
        case WORK_MODE_PLAYBACK:
            break;
        case WORK_MODE_USBCAM:
            break;
        case WORK_MODE_USB:
            break;
        default:
            break;
        }
        break;
    }
    case EVENT_MODEMNG_SMILE_START_PIV:
        break;
    case EVENT_MODEMNG_RECODER_STARTSTATU:
        page_vedio_on_recorder_started((int32_t)msg->arg1);
        break;
    case EVENT_MODEMNG_RECODER_STOPSTATU:
        page_vedio_on_recorder_stopped((int32_t)msg->arg1);
        break;
    case EVENT_MODEMNG_RECODER_SPLITREC:
        break;
    case EVENT_MODEMNG_RECODER_STARTEVENTSTAUE:
        break;
    case EVENT_MODEMNG_RECODER_STOPEVENTSTAUE:
        break;
    case EVENT_MODEMNG_CARD_FORMATING:
        ui_event_type = EVT_FORMAT_PROCESS;
        break;
    case EVENT_MODEMNG_CARD_FORMAT_SUCCESSED:
        ui_event_type = EVT_FORMAT_SUCCESS;
        break;
    case EVENT_MODEMNG_CARD_FORMAT_FAILED:
        ui_event_type = EVT_FORMAT_FAILED;
        break;
    case EVENT_MODEMNG_PLAYBACK_FINISHED:
        break;
    case EVENT_MODEMNG_PLAYBACK_PROGRESS:
        break;
    case EVENT_MODEMNG_PLAYBACK_PAUSE:
        break;
    case EVENT_MODEMNG_PLAYBACK_RESUME:
        break;
    case EVENT_MODEMNG_PLAYBACK_ABNORMAL:
        ui_event_type = EVT_FILE_ABNORMAL;
        break;
    case EVENT_MODETEST_START_RECORD:
        break;
    case EVENT_MODETEST_STOP_RECORD:
        break;
    case EVENT_MODETEST_PLAY_RECORD:
        break;
#ifdef SERVICES_IMAGE_VIEWER_ON
    case CVI_EVENT_AHDMNG_PLUG_STATUS: {
        if (msg->arg1 == CVI_AHDMNG_PLUG_OUT) {
            PARAM_SetMenuParam(0, PARAM_MENU_VIEW_WIN_STATUS, CVI_MEDIA_VIEW_WIN_FRONT);
        } else if (msg->arg1 == CVI_AHDMNG_PLUG_IN) {
            PARAM_SetMenuParam(0, PARAM_MENU_VIEW_WIN_STATUS, CVI_MEDIA_VIEW_WIN_DOUBLE);
        }
        break;
    }
#endif
    case EVENT_KEYMNG_LONG_CLICK:
        break;
    case EVENT_KEYMNG_SHORT_CLICK:
        break;
    case EVENT_UI_TOUCH: {
        if (g_is_mute_btn_voice)
            break;

        VOICEPLAY_VOICE_S stVoice = {
            .au32VoiceIdx = { UI_VOICE_TOUCH_BTN_IDX },
            .u32VoiceCnt = 1,
            .bDroppable = true,
        };
        VOICEPLAY_Push(&stVoice, 0);
        break;
    }
    case EVENT_MODEMNG_FOCUS: {
        if (getaction_audio_Index() == 0)
            break;
        VOICEPLAY_VOICE_S stVoice = {
            .au32VoiceIdx = { UI_VOICE_FOCUS_IDX },
            .u32VoiceCnt = 1,
            .bDroppable = true,
        };
        VOICEPLAY_Push(&stVoice, 0);
        break;
    }
    case EVENT_MODEMNG_PHOTO_STARTPIVSTAUE: {
#ifdef SERVICES_LIVEVIEW_ON
        if (getaction_audio_Index() == 0)
            break;
        if (msg->arg1 == 0) {
            VOICEPLAY_VOICE_S stVoice = {
                .au32VoiceIdx = { UI_VOICE_PHOTO_IDX },
                .u32VoiceCnt = 1,
                .bDroppable = true,
            };
            VOICEPLAY_Push(&stVoice, 0);
        }
#endif
        break;
    }
#ifdef SERVICES_SPEECH_ON
    case EVENT_MODEMNG_SPEECHMNG_STARTREC: {
        uint32_t u32ModeState = 0;
        MODEMNG_GetModeState(&u32ModeState);
        if (ui_common_cardstatus() == true && (u32ModeState != MEDIA_MOVIE_STATE_REC) && (u32ModeState != MEDIA_MOVIE_STATE_LAPSE_REC)) {
            Msg.topic = EVENT_MODEMNG_START_REC;
            MODEMNG_SendMessage(&Msg);
        }
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_STOPREC: {
        uint32_t u32ModeState = 0;
        MODEMNG_GetModeState(&u32ModeState);
        if ((u32ModeState == MEDIA_MOVIE_STATE_REC) || (u32ModeState == MEDIA_MOVIE_STATE_LAPSE_REC)) {
            Msg.topic = EVENT_MODEMNG_STOP_REC;
            MODEMNG_SendMessage(&Msg);
        }
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_OPENFRONT: {
        uint32_t curWind = (uint32_t)PARAM_Get_View_Win();
        if (curWind == 0) {
            break;
        }
        Msg.topic = EVENT_MODEMNG_SWITCH_LIVEVIEW;
        uint32_t enWind = (curWind >> 16) & 0xFFFF;
        uint32_t enSns = (curWind & 0xFFFF);
        if (enWind == 0x1) {
            break;
        }
        Msg.arg1 = ((0x1 << 16) | enSns);
        MODEMNG_SendMessage(&Msg);
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_OPENREAR: {
        uint32_t curWind = (uint32_t)PARAM_Get_View_Win();
        if (curWind == 0) {
            break;
        }
        Msg.topic = EVENT_MODEMNG_SWITCH_LIVEVIEW;
        uint32_t enWind = (curWind >> 16) & 0xFFFF;
        uint32_t enSns = (curWind & 0xFFFF);
        if (enWind == 0x10 || (enSns >> 1) != 0x1) {
            break;
        }
        Msg.arg1 = ((0x1 << 17) | enSns);
        MODEMNG_SendMessage(&Msg);
        break;
    }
#ifdef SCREEN_ON
    case EVENT_MODEMNG_SPEECHMNG_CLOSESCREEN: {
        s32Ret = HAL_SCREEN_COMM_SetBackLightState(HAL_SCREEN_IDXS_0, HAL_SCREEN_STATE_OFF);
        if (s32Ret != 0) {
            CVI_LOGI("Close screen fail\n");
        }
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_OPENSCREEN: {
        s32Ret = HAL_SCREEN_COMM_SetBackLightState(HAL_SCREEN_IDXS_0, HAL_SCREEN_STATE_ON);
        if (s32Ret != 0) {
            CVI_LOGI("Open screen fail\n");
        }
        break;
    }
#endif
    case EVENT_MODEMNG_SPEECHMNG_EMRREC: {
        uint32_t u32ModeState = 0;
        MODEMNG_GetModeState(&u32ModeState);
        if (u32ModeState != MEDIA_MOVIE_STATE_LAPSE_REC && ui_common_cardstatus()) {
            Msg.topic = EVENT_MODEMNG_START_EMRREC;
            MODEMNG_SendMessage(&Msg);
        }
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_PIV: {
        if (ui_common_cardstatus() == true) {
            Msg.topic = EVENT_MODEMNG_START_PIV;
            MODEMNG_SendMessage(&Msg);
        }
        break;
    }
#ifdef WIFI_ON
    case EVENT_MODEMNG_SPEECHMNG_CLOSEWIFI: {
        PARAM_WIFI_S WifiParam = { 0 };
        MESSAGE_S Msg = { 0 };
        PARAM_GetWifiParam(&WifiParam);
        if (WifiParam.Enable) {
            Msg.topic = EVENT_MODEMNG_SETTING;
            Msg.arg1 = PARAM_MENU_WIFI_STATUS;
            Msg.arg2 = 0;
            MODEMNG_SendMessage(&Msg);
            widget_t* topwin = window_manager_get_top_main_window(window_manager());
            if (topwin != NULL) {
                widget_t* wifi_image_widget = widget_lookup(topwin, "wifi_btm_image", TRUE);
                image_base_set_image(wifi_image_widget, "wifi_off");
            }
        }
        break;
    }
    case EVENT_MODEMNG_SPEECHMNG_OPENWIFI: {
        PARAM_WIFI_S WifiParam = { 0 };
        MESSAGE_S Msg = { 0 };
        PARAM_GetWifiParam(&WifiParam);
        if (!WifiParam.Enable) {
            Msg.topic = EVENT_MODEMNG_SETTING;
            Msg.arg1 = PARAM_MENU_WIFI_STATUS;
            Msg.arg2 = 1;
            MODEMNG_SendMessage(&Msg);
            widget_t* topwin = window_manager_get_top_main_window(window_manager());
            if (topwin != NULL) {
                widget_t* wifi_image_widget = widget_lookup(topwin, "wifi_btm_image", TRUE);
                image_base_set_image(wifi_image_widget, "wifi_on");
            }
        }
        break;
    }
#endif
#endif
#ifdef SERVICES_PHOTO_ON
    case EVENT_PHOTOMNG_PIV_END:
        MLOG_DBG("photo name: %s\n", msg->aszPayload);
        strncpy(pic_filepath, (char*)msg->aszPayload, sizeof(pic_filepath));
        ui_common_notify_piv_end();
        break;
    case EVENT_PHOTOMNG_PIV_ERROR:
        MLOG_ERR("photo error: %d\n", msg->arg1);
        memset(pic_filepath, 0, sizeof(pic_filepath));
        ui_common_notify_piv_end();
        break;
#endif
    case EVENT_FILEMNG_SPACE_FULL: {
        CVI_LOGE("sd space is full, please delete files!\n");
        MODEMNG_SetCardState(CARD_STATE_FULL_SPACE);
        break;
    }

    case EVENT_GAUGEMNG_LEVEL_CHANGE: {
        CVI_LOGD("msg->arg1 = %d msg->aszPayload = %s\n", msg->arg1, msg->aszPayload);
        set_batter_image_index(msg->arg1);
        break;
    }

    // 低电量报警，ini文件为27%
    case EVENT_GAUGEMNG_LEVEL_ULTRALOW: {
        CVI_LOGD("msg->arg1 = %d msg->aszPayload = %s\n", msg->arg1, msg->aszPayload);
        set_lowbatter_tips_flag(true);
        //ui处理
        break;
    }
    case EVENT_MODEMNG_SETTING:
        break;

    default:
        break;
    }

    return 0;
}

// 同步消息回调，用于设置结果并通知等待线程
static int32_t UICOMM_SyncMsgResultProc(EVENT_S* pstEvent)
{
    if (!s_stMessageCtx.bMsgProcessed) {
        g_sync_result = 0;
        g_sync_done = true;
        pthread_cond_signal(&g_sync_cond);
    }
    return g_sync_result;
}

int32_t UICOMM_SendSyncMsg(MESSAGE_S* pstMsg, uint32_t timeout_ms)
{
    int32_t s32Ret = 0;
    APPCOMM_CHECK_POINTER(pstMsg, -1);

    // 使用与 UICOMM_MessageResult 相同的锁
    MUTEX_LOCK(s_stMessageCtx.MsgMutex);

    if (!s_stMessageCtx.bMsgProcessed) {
        CVI_LOGE("[UICOMM_SendSyncMsg] Current Msg not finished\n");
        MUTEX_UNLOCK(s_stMessageCtx.MsgMutex);
        return -1;
    }

    // 重置同步状态
    g_sync_done = false;
    g_sync_result = -1;

    s_stMessageCtx.bMsgProcessed = false;
    s_stMessageCtx.stMsg.topic = pstMsg->topic;
    s_stMessageCtx.stMsg.arg1 = pstMsg->arg1;
    s_stMessageCtx.stMsg.arg2 = pstMsg->arg2;
    memcpy(s_stMessageCtx.stMsg.aszPayload, pstMsg->aszPayload, sizeof(s_stMessageCtx.stMsg.aszPayload));
    // 使用同步回调
    s_stMessageCtx.pfnMsgResultProc = UICOMM_SyncMsgResultProc;

    CVI_LOGD("[topic:%#x, arg1:%#x, arg2:%#x, timeout:%dms]\n", pstMsg->topic, pstMsg->arg1, pstMsg->arg2, timeout_ms);
    s32Ret = MODEMNG_SendMessage(pstMsg);

    if (0 != s32Ret) {
        CVI_LOGE("MODEMNG_SendMessage failed: %#x\n", s32Ret);
        s_stMessageCtx.bMsgProcessed = true;
        s_stMessageCtx.pfnMsgResultProc = NULL;
        MUTEX_UNLOCK(s_stMessageCtx.MsgMutex);
        return -1;
    }

    // 等待结果，设置超时
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
    }

    while (!g_sync_done) {
        s32Ret = pthread_cond_timedwait(&g_sync_cond, &s_stMessageCtx.MsgMutex, &ts);
        if (s32Ret == ETIMEDOUT) {
            CVI_LOGW("Wait sync msg result timeout: %dms\n", timeout_ms);
            s_stMessageCtx.bMsgProcessed = true;
            s_stMessageCtx.pfnMsgResultProc = NULL;
            MUTEX_UNLOCK(s_stMessageCtx.MsgMutex);
            return -1; // 超时返回错误
        }
    }

    s32Ret = g_sync_result;
    s_stMessageCtx.bMsgProcessed = true;
    s_stMessageCtx.pfnMsgResultProc = NULL;
    MUTEX_UNLOCK(s_stMessageCtx.MsgMutex);

    CVI_LOGD("Sync msg result: %d\n", s32Ret);
    return s32Ret;
}

int32_t ui_common_SubscribeEvents(void)
{
    int32_t ret = 0;
    uint32_t i = 0;
    EVENTHUB_SUBSCRIBER_S stSubscriber = { "ui", NULL, ui_common_eventcb, false };
    MW_PTR SubscriberHdl = NULL;
    TOPIC_ID topic[] = {
        EVENT_MODEMNG_CARD_REMOVE,
        EVENT_MODEMNG_CARD_AVAILABLE,
        EVENT_MODEMNG_CARD_UNAVAILABLE,
        EVENT_MODEMNG_CARD_ERROR,
        EVENT_MODEMNG_CARD_FSERROR,
        EVENT_MODEMNG_CARD_SLOW,
        EVENT_MODEMNG_CARD_CHECKING,
        EVENT_MODEMNG_CARD_FORMAT,
        EVENT_MODEMNG_CARD_FORMATING,
        EVENT_MODEMNG_CARD_FORMAT_SUCCESSED,
        EVENT_MODEMNG_CARD_FORMAT_FAILED,
        EVENT_MODEMNG_CARD_READ_ONLY,
        EVENT_MODEMNG_CARD_MOUNT_FAILED,
        EVENT_MODEMNG_RESET,
        EVENT_MODEMNG_MODESWITCH,
        EVENT_MODEMNG_MODEOPEN,
        EVENT_MODEMNG_MODECLOSE,
        EVENT_MODEMNG_SETTING,
        EVENT_MODEMNG_FOCUS,
        EVENT_MODEMNG_PHOTO_STARTPIVSTAUE,
        EVENT_PHOTOMNG_PIV_END,
        EVENT_PHOTOMNG_PIV_ERROR,
        EVENT_MODEMNG_PLAYBACK_FINISHED,
        EVENT_MODEMNG_PLAYBACK_PROGRESS,
        EVENT_MODEMNG_PLAYBACK_PAUSE,
        EVENT_MODEMNG_PLAYBACK_RESUME,
        EVENT_MODEMNG_PLAYBACK_ABNORMAL,
        EVENT_MODETEST_START_RECORD,
        EVENT_MODETEST_STOP_RECORD,
        EVENT_MODETEST_PLAY_RECORD,
        EVENT_MODEMNG_RECODER_STARTSTATU,
        EVENT_MODEMNG_RECODER_STOPSTATU,
        EVENT_MODEMNG_RECODER_SPLITREC,
        EVENT_MODEMNG_RECODER_STARTEVENTSTAUE,
        EVENT_MODEMNG_RECODER_STOPEVENTSTAUE,
        EVENT_MODEMNG_RECODER_STARTEMRSTAUE,
        EVENT_MODEMNG_RECODER_STOPEMRSTAUE,
        EVENT_MODEMNG_RECODER_STARTPIVSTAUE,
        EVENT_FILEMNG_SPACE_FULL,
#ifdef SERVICES_IMAGE_VIEWER_ON
        CVI_EVENT_AHDMNG_PLUG_STATUS,
#endif
        EVENT_KEYMNG_LONG_CLICK,
        EVENT_KEYMNG_SHORT_CLICK,
        EVENT_UI_TOUCH,
        EVENT_STORAGEMNG_DEV_CONNECTING,
        WORK_MODE_UVC,
        WORK_MODE_STORAGE,
        EVENT_MODEMNG_UVC_MODE_START,
        EVENT_MODEMNG_STORAGE_MODE_PREPAREDEV,
        EVENT_GAUGEMNG_LEVEL_CHANGE,
        EVENT_GAUGEMNG_LEVEL_LOW,
        EVENT_GAUGEMNG_LEVEL_ULTRALOW,
        EVENT_GAUGEMNG_LEVEL_NORMAL,
    };
    ret = EVENTHUB_RegisterTopic(EVENT_UI_TOUCH);
    APPCOMM_CHECK_RETURN(ret, ret);

    ret = EVENTHUB_CreateSubscriber(&stSubscriber, &SubscriberHdl);
    if (ret != 0) {
        CVI_LOGE("EVENTHUB_CreateSubscriber failed! \n");
    }

    uint32_t u32ArraySize = UI_ARRAY_SIZE(topic);

    for (i = 0; i < u32ArraySize; i++) {
        ret = EVENTHUB_Subcribe(SubscriberHdl, topic[i]);
        if (ret) {
            CVI_LOGE("Subscribe topic(%#x) failed. %#x\n", topic[i], ret);
            continue;
        }
    }

    return ret;
}

void ui_common_mute_btn_voice(void)
{
    g_is_mute_btn_voice = 1;
}

void ui_common_unmute_btn_voice(void)
{
    g_is_mute_btn_voice = 0;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
