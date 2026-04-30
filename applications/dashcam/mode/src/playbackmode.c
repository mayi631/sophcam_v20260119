#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "modeinner.h"
#include "volmng.h"
#include "media_disp.h"

int32_t MODEMNG_OpenPlayBackMode(void)
{
#ifdef SERVICES_PLAYER_ON
    int32_t s32Ret = 0;
    MODEMNG_S* pstModeMngCtx = MODEMNG_GetModeCtx();

    s32Ret = MEDIA_VIVPSSInitPlayBack();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_VIVPSSInitPlayBack fail");

    // s32Ret = MEDIA_DispInit(false);
    // MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_DispInit fail");

    s32Ret = MEDIA_PlayBackSerInit();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_PlayBackSerInit fail");

    /* Clear leftover preview frame before playback display resumes. */
    s32Ret = MEDIA_DISP_ClearBuf();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "DISP clear");

    MEDIA_PARAM_INIT_S *SysMediaParams = MEDIA_GetCtx();
    PLAYER_SERVICE_HANDLE_T PlaySerhdl = SysMediaParams->SysServices.PsHdl;

    s32Ret = PLAYER_SERVICE_SetEventHandler(PlaySerhdl, PLAYBACKMNG_EventCallBack);
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "PLAYER_SERVICE_SetEventHandler fail");

    s32Ret = MEDIA_DISP_Resume();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "DISP resume");

    EVENT_S stEvent;
    stEvent.topic = EVENT_MODEMNG_MODEOPEN;
    stEvent.arg1 = WORK_MODE_PLAYBACK;
    EVENTHUB_Publish(&stEvent);

    pstModeMngCtx->CurWorkMode = WORK_MODE_PLAYBACK;

    //set open amplifiler
    VOICEPLAY_SetAmplifier(false);
#endif
    return 0;
}

int32_t MODEMNG_ClosePlayBackMode(void)
{
#ifdef SERVICES_LIVEVIEW_ON
    int32_t s32Ret = 0;

    MODEMNG_S* pstModeMngCtx = MODEMNG_GetModeCtx();

    s32Ret = MEDIA_DISP_Pause();
    MODEMNG_CHECK_RET(s32Ret,MODE_EINVAL,"DISP pause");

    /* Clear playback's last shown frame before switching back to preview. */
    s32Ret = MEDIA_DISP_ClearBuf();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "DISP clear");

    pstModeMngCtx->CurWorkMode = WORK_MODE_BUTT;
    pstModeMngCtx->u32ModeState = MEDIA_MOVIE_STATE_BUTT;

    s32Ret = MEDIA_PlayBackSerDeInit();
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_PlayBackSerDeInit fail");

    // s32Ret = MEDIA_DispDeInit();
    // MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_DispDeInit fail");


    // s32Ret = MEDIA_VbDeInit();
    // MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MEDIA_VbDeInit fail");

    // if (CARD_STATE_AVAILABLE == MODEMNG_GetCardState()) {
    //     s32Ret = MODEMNG_PlayBackModeScanFile();
    //     MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "MODEMNG_PlayBackModeScanFile fail");
    // }

    EVENT_S stEvent;
    stEvent.topic = EVENT_MODEMNG_MODECLOSE;
    stEvent.arg1 = WORK_MODE_PLAYBACK;
    EVENTHUB_Publish(&stEvent);

    //set close amplifiler
    // VOICEPLAY_SetAmplifier(true);
#endif

    return 0;
}

/** Playback Mode message process */
int32_t MODEMNG_PlaybackModeMsgProc(MESSAGE_S* pstMsg, void* pvArg, uint32_t* pStateID)
{
#ifdef SERVICES_PLAYER_ON
    // int32_t s32Ret = 0;
    MODEMNG_S* pstModeMngCtx = MODEMNG_GetModeCtx();

    if (pstModeMngCtx->bSysPowerOff == true) {
        CVI_LOGI("power off ignore msg id: %x\n", pstMsg->topic);
        return PROCESS_MSG_RESULTE_OK;
    }

    /** check parameters */
    MODEMNG_CHECK_MSGPROC_FUNC_PARAM(pvArg, pStateID, pstMsg, pstModeMngCtx->bInProgress);

    STATE_S* pstStateAttr = (STATE_S*)pvArg;
    CVI_LOGI("MODEMNG_PlaybackModeMsgProc:%s\n", event_topic_get_name(pstMsg->topic));
    (void)pstStateAttr;
    switch (pstMsg->topic) {
    case EVENT_PLAYBACKMNG_PLAY: {
        MODEMNG_MonitorStatusNotify(pstMsg);
        pstModeMngCtx->u32ModeState = MEDIA_PLAYBACK_STATE_PLAY;
        return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_PLAYBACKMNG_FINISHED:
        {
            if(CARD_STATE_REMOVE == MODEMNG_GetCardState()) {
                CVI_LOGD("Card Removed, Not process event:%x\n",
                    EVENT_PLAYBACKMNG_FINISHED);
                return PROCESS_MSG_UNHANDLER;
            }
            pstModeMngCtx->u32ModeState = MEDIA_PLAYBACK_STATE_VIEW;
            MODEMNG_MonitorStatusNotify(pstMsg);
            return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_PLAYBACKMNG_PROGRESS:
        {
            MODEMNG_MonitorStatusNotify(pstMsg);
            return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_PLAYBACKMNG_PAUSE:
        {
            MODEMNG_MonitorStatusNotify(pstMsg);
            pstModeMngCtx->u32ModeState = MEDIA_PLAYBACK_STATE_PAUSE;
            return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_PLAYBACKMNG_RESUME:
        {
            MODEMNG_MonitorStatusNotify(pstMsg);
            pstModeMngCtx->u32ModeState = MEDIA_PLAYBACK_STATE_PLAY;
            return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_PLAYBACKMNG_FILE_ABNORMAL:
        {
            MODEMNG_MonitorStatusNotify(pstMsg);
            return PROCESS_MSG_RESULTE_OK;
        }
        case EVENT_SENSOR_PLUG_STATUS:
        {
            PARAM_CFG_S Param;
            PARAM_GetParam(&Param);
            int32_t snsid = pstMsg->aszPayload[1];
            int32_t mode = pstMsg->aszPayload[0];
            if (SENSOR_PLUG_IN == pstMsg->arg1) {
                CVI_LOGD("sensor %d plug in\n", snsid);
                CVI_LOGD("sensor %d resolution=%d\n", snsid, mode);
                Param.CamCfg[snsid].CamMediaInfo.CurMediaMode = MEDIA_Res2RecordMediaMode(mode);
                Param.WorkModeCfg.RecordMode.CamMediaInfo[snsid].CurMediaMode = MEDIA_Res2RecordMediaMode(mode);
                Param.WorkModeCfg.PhotoMode.CamMediaInfo[snsid].CurMediaMode = MEDIA_Res2PhotoMediaMode(mode);
                Param.CamCfg[snsid].CamEnable = true;
                #ifdef SERVICES_LIVEVIEW_ON
                Param.MediaComm.Window.Wnds[snsid].WndEnable = true;
                #endif
                MAPI_VCAP_SetAhdMode(snsid, mode);
            } else if (SENSOR_PLUG_OUT == pstMsg->arg1) {
                CVI_LOGD("sensor %d plug out\n", snsid);
                Param.CamCfg[snsid].CamEnable = false;
                #ifdef SERVICES_LIVEVIEW_ON
                Param.MediaComm.Window.Wnds[snsid].WndEnable = false;
                #endif
            }
            PARAM_SetParam(&Param);
            return PROCESS_MSG_RESULTE_OK;
        }
        default:
            return PROCESS_MSG_UNHANDLER;
        }
#endif
    return PROCESS_MSG_RESULTE_OK;
}


int32_t MODEMNG_PlaybackStatesInit(const STATE_S* pstBase)
{
    int32_t s32Ret = 0;
#ifdef SERVICES_PLAYER_ON
    static STATE_S stPlaybackState =
    {
        WORK_MODE_PLAYBACK,
        MODEEMNG_STATE_PLAYBACK,
        MODEMNG_OpenPlayBackMode,
        MODEMNG_ClosePlayBackMode,
        MODEMNG_PlaybackModeMsgProc,
        NULL
    };
    stPlaybackState.argv = &stPlaybackState;
    s32Ret = HFSM_AddState(MODEMNG_GetModeCtx()->pvHfsmHdl,
                              &stPlaybackState,
                              (STATE_S*)pstBase);
    MODEMNG_CHECK_RET(s32Ret, MODE_EINVAL, "HFSM add NormalRec state");
#endif

    return s32Ret;
}

/** deinit playback mode */
int32_t MODEMNG_PlaybackStatesDeinit(void)
{
    int32_t s32Ret = 0;
    MODEMNG_ClosePlayBackMode();
    return s32Ret;
}
