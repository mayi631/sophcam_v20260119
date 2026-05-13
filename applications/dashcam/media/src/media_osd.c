#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/prctl.h>
#include "cvi_type.h"
#include "cvi_comm_video.h"
#include "cvi_ae.h"
#include "cvi_awb.h"
#include "mapi.h"
#include "media_osd.h"
#include "appcomm.h"
#include "param.h"
#include "media_init.h"
#include "bitmap.h"
#ifdef SERVICES_ANIP_ON
#include "anip_service.h"
#endif
#ifdef GPS_ON
#include "gpsmng.h"
static GPSMNG_CALLBACK GpsCallback = {0};
#endif

#define FONTMOD_WIDTH 24
#define FONTMOD_HEIGHT 30
#define MEDIA_MAX_INFO_OSD_LEN        (120)
#define MEDIA_MAX_GPS_OSD_CNT         (2)

#ifdef ENABLE_ISP_PQ_TOOL
#define ISP_INFO_OSD
#endif

#define ISP_INFO_LINES_CNT 2
#define OSD_INFO_START_X        (0)
#define OSD_INFO_START_Y        (75)
#define OSD_INFO_FONT_WIDTH     (16)
#define OSD_INFO_FONT_HEIGHT    (24)

typedef struct tagcvi_MEDIA_OSDCtx
{
    bool init;
    PARAM_MEDIA_OSD_ATTR_S osdCfg;
#if defined(ISP_INFO_OSD)
    int32_t vcapDevIdx[MAX_CAMERA_INSTANCES];
    bool infoTskRun[MAX_CAMERA_INSTANCES];
    pthread_t infoTskId[MAX_CAMERA_INSTANCES];
    MAPI_OSD_ATTR_S infoOsdAttr[MAX_CAMERA_INSTANCES][ISP_INFO_LINES_CNT];
#endif
} MEDIA_OSDCtx;
static MEDIA_OSDCtx g_mediaOsdCtx;

#if defined(ISP_INFO_OSD)
static void MEDIA_OSD_GetInfoStr(int32_t viPipe, char* str, uint32_t strLen, int32_t line)
{
    MEDIA_SYSHANDLE_S *Syshdl = &MEDIA_GetCtx()->SysHandle;
    int32_t status = 0;
    MAPI_VCAP_GetSensorPipeAttr(Syshdl->sns[viPipe], &status);
    if (0 == status) {
        CVI_LOGE("stViPipeAttr.bYuvBypassPath is true, yuv sensor skip isp ops");
    }

    ISP_EXP_INFO_S expInfo;
    ISP_WB_INFO_S wbInfo;
    memset(&expInfo,0,sizeof(ISP_EXP_INFO_S));
    memset(&wbInfo,0,sizeof(ISP_WB_INFO_S));
    CVI_ISP_QueryExposureInfo(viPipe, &expInfo);
    CVI_ISP_QueryWBInfo(viPipe, &wbInfo);
    if(line == 0) {
        snprintf(str, strLen, "#AE ExpT:%u SExpT:%u LExpT:%u AG:%u DG:%u IG:%u LV:%.3f Exp:%u ExpIsMax:%d AveLum:%d",
                expInfo.u32ExpTime, expInfo.u32ShortExpTime, expInfo.u32LongExpTime, expInfo.u32AGain,\
                expInfo.u32DGain, expInfo.u32ISPDGain, expInfo.fLightValue, expInfo.u32Exposure, expInfo.bExposureIsMAX, \
                expInfo.u8AveLum);
        //CVI_LOGD("ViPipe[%d], DebugStr[%s]\n", viPipe, str);
    } else if (line == 1){
        snprintf(str, strLen, "PIrisFno:%d Fps:%u ISO:%u #AWB RG:%d BG:%d CT:%d",
                expInfo.u32PirisFNO, expInfo.u32Fps, expInfo.u32ISO,
                wbInfo.u16Rgain, wbInfo.u16Bgain, wbInfo.u16ColorTemp);
        //CVI_LOGD("ViPipe[%d], DebugStr[%s]\n", viPipe, str);
    }
}

static void* MEDIA_OSD_UpdateInfoTsk(void* param)
{
    prctl(PR_SET_NAME, "OSDInfoTsk", 0, 0, 0);
    int32_t ret = 0;
    PARAM_MEDIA_OSD_ATTR_S* osdCfg = &g_mediaOsdCtx.osdCfg;
    int32_t devIndex = *(int32_t*)param;
    int32_t viPipe =  0;
    int32_t tempid[2] = {0};
    int32_t osdIdx = osdCfg->OsdCnt + (devIndex * ISP_INFO_LINES_CNT);

    int32_t i = 0;
    for(i = 0;i < ISP_INFO_LINES_CNT; i++) {
        MAPI_OSD_ATTR_S * infoOsdAttr = &g_mediaOsdCtx.infoOsdAttr[devIndex][i];

        MEDIA_OSD_GetInfoStr(viPipe, infoOsdAttr->stContent.stStrContent.szStr, MEDIA_MAX_INFO_OSD_LEN, i);
        ret = MAPI_OSD_SetAttr(osdIdx, infoOsdAttr);
        if (ret == 0) {
            ret = MAPI_OSD_Start(osdIdx);
            APPCOMM_CHECK_RETURN(ret, NULL);
        } else {
            return NULL;
        }
        tempid[i] = osdIdx;
        osdIdx++;
    }

    while (g_mediaOsdCtx.infoTskRun[devIndex]) {
        for(i = 0;i < ISP_INFO_LINES_CNT; i++) {
            MAPI_OSD_ATTR_S * infoOsdAttr = &g_mediaOsdCtx.infoOsdAttr[devIndex][i];
            osdIdx = tempid[i];
            MEDIA_OSD_GetInfoStr(viPipe, infoOsdAttr->stContent.stStrContent.szStr, MEDIA_MAX_INFO_OSD_LEN, i);
            ret = MAPI_OSD_SetAttr(osdIdx, infoOsdAttr);
            if (ret != 0) {
                CVI_LOGE("MAPI_OSD_SetAttr[%d] failed %x\n", osdIdx,ret);
                break;
            }
        }
        usleep(1*100*1000);
    }

    for(i = 0;i < ISP_INFO_LINES_CNT; i++){
        MAPI_OSD_Stop(tempid[i]);
    }
    return NULL;
}
#endif

#if defined(ISP_INFO_OSD)
static int32_t MEDIA_StartInfoOsd(void)
{
    int32_t ret = 0;
    PARAM_MEDIA_OSD_ATTR_S* osdCfg = &g_mediaOsdCtx.osdCfg;
    int32_t i = 0, j = 0;
    for (i = 0; i < MAX_CAMERA_INSTANCES - 1; ++i) {
    #ifdef RESET_MODE_AHD_HOTPLUG_ON
        if (MEDIA_Is_CameraEnabled(i) == false) {
            continue;
        }
    #endif
        g_mediaOsdCtx.vcapDevIdx[i] = i;
        for (j = 0; j < ISP_INFO_LINES_CNT; ++j) {
            /* Use before TimeOsd attr to fill InfoOsd Attr*/
            MAPI_OSD_ATTR_S * infoOsdAttr = &g_mediaOsdCtx.infoOsdAttr[i][j];
            infoOsdAttr->u32DispNum = 1;
            infoOsdAttr->astDispAttr[0].bShow = 1;
            infoOsdAttr->astDispAttr[0].enBindedMod = 0;//0 - vproc mode 1 - disp mode
            if(i == 0) {
                infoOsdAttr->astDispAttr[0].ModHdl = i;//vproc id
                infoOsdAttr->astDispAttr[0].ChnHdl = 0;//vproc chnid
                infoOsdAttr->astDispAttr[0].u32Batch = osdCfg->OsdCnt + j;
            } else if (i == 1) {
                infoOsdAttr->astDispAttr[0].ModHdl = i;//vproc id
                infoOsdAttr->astDispAttr[0].ChnHdl = 0;//vproc chnid
                infoOsdAttr->astDispAttr[0].u32Batch = osdCfg->OsdCnt + ISP_INFO_LINES_CNT + j;
            }
            infoOsdAttr->stContent.u32Color = 0xffff;//argb1555
            infoOsdAttr->stContent.stStrContent.u32BgColor = 0x8000;//argb1555
            infoOsdAttr->stContent.stStrContent.stFontSize.u32Width = OSD_INFO_FONT_WIDTH;
            infoOsdAttr->stContent.stStrContent.stFontSize.u32Height = OSD_INFO_FONT_HEIGHT;
            infoOsdAttr->stContent.enType = MAPI_OSD_TYPE_STRING;
            infoOsdAttr->astDispAttr[0].enCoordinate = MAPI_OSD_COORDINATE_RATIO_COOR;
            infoOsdAttr->astDispAttr[0].stStartPos.s32X = OSD_INFO_START_X;
            infoOsdAttr->astDispAttr[0].stStartPos.s32Y = OSD_INFO_START_Y + 3 * j;
        }
        MAPI_OSD_ATTR_S * infoOsdAttr = &g_mediaOsdCtx.infoOsdAttr[i][0];
        if (infoOsdAttr->astDispAttr[0].bShow && g_mediaOsdCtx.infoTskId[i] == 0) {
            g_mediaOsdCtx.infoTskRun[i] = true;
            ret = pthread_create(&g_mediaOsdCtx.infoTskId[i], NULL, MEDIA_OSD_UpdateInfoTsk, &g_mediaOsdCtx.vcapDevIdx[i]);
            if (ret != 0) {
                g_mediaOsdCtx.infoTskRun[i] = false;
                CVI_LOGE( "s32VcapDevIdx[%d] create OSD_UpdateInfoThread failed %x\n", g_mediaOsdCtx.vcapDevIdx[i],ret);
                return ret;
            }
        }
    }
    return 0;
}
#endif

int32_t MEDIA_StopInfoOsd(void)
{
#if defined(ISP_INFO_OSD)
    for (int32_t i = 0; i < MAX_CAMERA_INSTANCES ; ++i) {
    #ifdef RESET_MODE_AHD_HOTPLUG_ON
        if (MEDIA_Is_CameraEnabled(i) == false) {
            continue;
        }
    #endif
        if(g_mediaOsdCtx.infoTskRun[i] == true){
            g_mediaOsdCtx.infoTskRun[i] = false;
            pthread_join(g_mediaOsdCtx.infoTskId[i], NULL);
            g_mediaOsdCtx.infoTskId[i] = 0;
            g_mediaOsdCtx.vcapDevIdx[i] = 0;
        }
    }
#endif
    return 0;
}

#ifdef GPS_ON
static int32_t MEDIA_GPSMsgHandler(GPSMNG_MSG_PACKET *msgPacket, void* privateData)
{
    int32_t i = 0;
    int32_t s32GpsOsdId[MEDIA_MAX_GPS_OSD_CNT] = {4,5};
    PARAM_MENU_S MenuParam = {0};
    PARAM_GetMenuParam(&MenuParam);
    for (; i < MEDIA_MAX_GPS_OSD_CNT; ++i) {
        if ((msgPacket->gpsRMC.status == 'A') && ((MenuParam.GPSStamp.Current == MENU_SPEEDSTAMP_ON) ||
                (MenuParam.SpeedStamp.Current == MENU_GPSSTAMP_ON))) {
        // if ((MenuParam.GPSStamp.Current == MENU_SPEEDSTAMP_ON) || (MenuParam.SpeedStamp.Current == MENU_GPSSTAMP_ON)) {
            MAPI_OSD_ATTR_S OsdAttr = {0};
            char latitude, longitude;
            latitude = msgPacket->gpsRMC.ns;
            longitude = msgPacket->gpsRMC.ew;
            MAPI_OSD_GetAttr(s32GpsOsdId[i], &OsdAttr);
            if ((MenuParam.GPSStamp.Current == MENU_SPEEDSTAMP_ON) &&
                (MenuParam.SpeedStamp.Current == MENU_GPSSTAMP_ON)) {
                snprintf(OsdAttr.stContent.stStrContent.szStr, MEDIA_MAX_INFO_OSD_LEN, "%03fKM/H %C:%f %C:%f",
                msgPacket->gpsRMC.speed, latitude, msgPacket->gpsRMC.lat, longitude, msgPacket->gpsRMC.lon);
                OsdAttr.astDispAttr[0].bShow = true;
            } else if((MenuParam.GPSStamp.Current == MENU_SPEEDSTAMP_OFF) &&
                (MenuParam.SpeedStamp.Current == MENU_GPSSTAMP_ON)) {
                snprintf(OsdAttr.stContent.stStrContent.szStr, MEDIA_MAX_INFO_OSD_LEN, "%C:%f %C:%f",
                latitude, msgPacket->gpsRMC.lat, longitude, msgPacket->gpsRMC.lon);
                OsdAttr.astDispAttr[0].bShow = true;
            } else if((MenuParam.GPSStamp.Current == MENU_SPEEDSTAMP_ON) &&
                (MenuParam.SpeedStamp.Current == MENU_GPSSTAMP_OFF)) {
                snprintf(OsdAttr.stContent.stStrContent.szStr, MEDIA_MAX_INFO_OSD_LEN, "%03fKM/H",
                msgPacket->gpsRMC.speed);
                OsdAttr.astDispAttr[0].bShow = true;
            }

            MAPI_OSD_SetAttr(s32GpsOsdId[i], &OsdAttr);
        } else {
            MAPI_OSD_Show(s32GpsOsdId[i], 0, false);
        }

    }

    return 0;
}
#endif

#ifdef SERVICES_FACEP_ON
CVI_S32 MEDIA_DrawRects(CVI_U32 osd_id, CVI_U32 num, RECT_S* rects){
    CVI_S32 ret = CVI_SUCCESS;
    CVI_U32 j = 0, k = 0;
    PARAM_MEDIA_OSD_ATTR_S osd_param = {0};
    CVI_MAPI_OSD_OBJECT_CONTENT_S* pst_object = NULL;

    PARAM_GetOsdParam(&osd_param);

    MAPI_OSD_ATTR_S osd_attr = {0};
    ret = MAPI_OSD_GetAttr(osd_id, &osd_attr);
    if(ret != MAPI_SUCCESS){
        CVI_LOGE("Get_attr faild\n");
        return ret;
    }
    pst_object = &osd_attr.stContent.stObjectContent;
    for(j = 0; j < osd_param.OsdAttrs[osd_id].u32DispNum; j++){
        if ((osd_param.OsdAttrs[osd_id].astDispAttr[j].bShow == 1) &&
        osd_param.OsdAttrs[osd_id].stContent.enType == MAPI_OSD_TYPE_OBJECT){
            for (k = 0; k < num; k++) {
                (pst_object->objectInfo.rec_coordinates)[4*k] = rects[k].s32X;
                (pst_object->objectInfo.rec_coordinates)[4*k+1] = rects[k].s32Y;
                (pst_object->objectInfo.rec_coordinates)[4*k+2] = rects[k].u32Width;//x2
                (pst_object->objectInfo.rec_coordinates)[4*k+3] = rects[k].u32Height;//y2
            }

            pst_object->objectInfo.rec_cnt = k;
            pst_object->objectInfo.line_cnt = 0;

            ret = MAPI_OSD_SetAttr(osd_id, &osd_attr);
            if(ret != MAPI_SUCCESS){
                CVI_LOGE("Set_attr faild\n");
                continue;
            }
        }
    }

    return ret;
}
#endif

int32_t MEDIA_StartOsd(void)
{
    int32_t ret = 0;
    int32_t osdIdx = 0;
    uint32_t osdDispCount = 0;

    //MAPI_OSD_ATTR_S osdAttr = {0};
    MAPI_OSD_FONTS_S pstFonts;
    pstFonts.u32FontWidth = FONTMOD_WIDTH;
    pstFonts.u32FontHeight = FONTMOD_HEIGHT;
    PARAM_MEDIA_OSD_ATTR_S pstOsdCfg;
    PARAM_GetOsdParam(&pstOsdCfg);
    MAPI_OSD_Init(&pstFonts, pstOsdCfg.bOsdc);

    for (osdIdx = 0; osdIdx < pstOsdCfg.OsdCnt; ++osdIdx) {
        osdDispCount = 0;
        for (uint32_t j = 0; j < pstOsdCfg.OsdAttrs[osdIdx].u32DispNum; j++) {
            int32_t vproc_id = pstOsdCfg.OsdAttrs[osdIdx].astDispAttr[j].ModHdl;
            int32_t vproc_chn = pstOsdCfg.OsdAttrs[osdIdx].astDispAttr[j].ChnHdl;
            MAPI_VPROC_HANDLE_T vproc_hdl = MEDIA_GetCtx()->SysHandle.vproc[vproc_id];
            if (MAPI_VPROC_IsExtChn(vproc_hdl, vproc_chn)) {
                vproc_id = MAPI_VPROC_GetExtChnGrp(vproc_hdl, vproc_chn);
                vproc_chn = 0;
            }
            pstOsdCfg.OsdAttrs[osdIdx].astDispAttr[j].ModHdl = vproc_id;
            pstOsdCfg.OsdAttrs[osdIdx].astDispAttr[j].ChnHdl = vproc_chn;
            osdDispCount++;
        }
        pstOsdCfg.OsdAttrs[osdIdx].u32DispNum = osdDispCount;

        if (pstOsdCfg.OsdAttrs[osdIdx].u32DispNum == 0) {
            CVI_LOGW("OsdIdx[%d] content disp num is zero.This content do nothing.\n",osdIdx);
            continue;
        }

        if (pstOsdCfg.OsdAttrs[osdIdx].stContent.enType == MAPI_OSD_TYPE_BITMAP) {
            pstOsdCfg.OsdAttrs[osdIdx].stContent.stBitmapContent.pData = pdata;
            pstOsdCfg.OsdAttrs[osdIdx].stContent.stBitmapContent.u32Width = stBitMapSize.Width;
            pstOsdCfg.OsdAttrs[osdIdx].stContent.stBitmapContent.u32Height = stBitMapSize.Height;
        }

        ret = MAPI_OSD_SetAttr(osdIdx, &pstOsdCfg.OsdAttrs[osdIdx]);
        if (ret == 0) {
            ret = MAPI_OSD_Start(osdIdx);
        } else {
            CVI_LOGW("OsdIdx[%d]: No Osdfont.\n",osdIdx);
            return ret;
        }
    }

    memcpy(&g_mediaOsdCtx.osdCfg, &pstOsdCfg, sizeof(PARAM_MEDIA_OSD_ATTR_S));
#if defined(ISP_INFO_OSD)
    MEDIA_StartInfoOsd();
#endif

#ifdef GPS_ON
    GpsCallback.fnGpsDataCB = MEDIA_GPSMsgHandler;
    GpsCallback.privateData = NULL;
    GPSMNG_Register(&GpsCallback);
#endif

#ifdef SERVICES_FACEP_ON
    FACEP_SERVICE_Register_DrawRects_Callback(MEDIA_DrawRects);
#endif

#ifdef SERVICES_ANIP_ON
    ANIP_SERVICE_Register_DrawRects_Callback(MEDIA_DrawRects);
#endif

    g_mediaOsdCtx.init = true;
    return 0;
}

int32_t MEDIA_UpdateCarNumOsd(void)
{
    PARAM_CONTEXT_S *pstParamCtx = PARAM_GetCtx();
    PARAM_MEDIA_OSD_ATTR_S OsdParam;
    uint32_t i = 0, z = 0 ,s32Ret = 0, value = 0;
    int32_t j = 0;
    value = pstParamCtx->pstCfg->Menu.CarNumStamp.Current;
    PARAM_GetOsdParam(&OsdParam);
    //OsdParam = pstParamCtx->pstCfg->MediaComm.Osd;
    CVI_LOGD("set param :value = %d\n",value);
    for(i = 0; i < MAX_CAMERA_INSTANCES; i++) {
    #ifdef RESET_MODE_AHD_HOTPLUG_ON
        if (MEDIA_Is_CameraEnabled(i) == false) {
            continue;
        }
    #endif
        for(j = 0; j < OsdParam.OsdCnt; j++){
            for(z = 0; z < OsdParam.OsdAttrs[j].u32DispNum; z++){
                if (OsdParam.OsdAttrs[j].astDispAttr[z].u32Batch == i &&
                    OsdParam.OsdAttrs[j].stContent.enType == MAPI_OSD_TYPE_STRING){
                    // printf("update carname = %s\n", OsdParam.OsdAttrs[j].stContent.stStrContent.szStr);
                    s32Ret = MAPI_OSD_SetAttr(j, &OsdParam.OsdAttrs[j]);
                    if(s32Ret == 0){
                         MAPI_OSD_Show(j, z, value);
                        //  CVI_LOGD("show finsh param :value = %d\n",value);
                    }else{
                        CVI_LOGE("set_attr faild\n");
                    }
                    OsdParam.OsdAttrs[j].astDispAttr[z].bShow = value;
                }
            }
        }
    }
    PARAM_SetOsdParam(&OsdParam);

    return 0;
}

int32_t MEDIA_StopOsd(void)
{
    if(g_mediaOsdCtx.init == false) {
        CVI_LOGE("media osd not init \n");
        return -1;
    }
#ifdef GPS_ON
    GPSMNG_UnRegister(&GpsCallback);
#endif



    int32_t ret = 0;
    PARAM_MEDIA_OSD_ATTR_S* osdCfg = &g_mediaOsdCtx.osdCfg;
    int32_t osdIdx = 0;
    for (osdIdx = 0; osdIdx < osdCfg->OsdCnt ; ++osdIdx) {
        CVI_LOGD("s32OsdIdx=%d\n",osdIdx);
        ret = MAPI_OSD_Stop(osdIdx);
        APPCOMM_CHECK_RETURN(ret, ret);
    }

#if defined(ISP_INFO_OSD)
    MEDIA_StopInfoOsd();
#endif
    MAPI_OSD_Deinit();

    return 0;
}
