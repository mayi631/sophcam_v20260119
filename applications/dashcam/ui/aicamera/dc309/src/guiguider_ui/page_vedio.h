
#ifndef __PAGE_VEDIO_H_
#define __PAGE_VEDIO_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

extern lv_obj_t *obj_vedio_s; //底层窗口

void video_effect_scr_delete(void);
void Home_Vedio(lv_ui_t *ui);

/* EVENT_MODEMNG_RECODER_STARTSTATU 时调用；rec_id 为主路录像通道（一般为 0） */
void page_vedio_on_recorder_started(int32_t rec_id);
/* EVENT_MODEMNG_RECODER_STOPSTATU 时调用；rec_id 为主路录像通道（一般为 0） */
void page_vedio_on_recorder_stopped(int32_t rec_id);

#ifdef __cplusplus
}
#endif
#endif /* EVENT_CB_H_ */
