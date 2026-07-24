#ifndef __APP_SCREEN_H
#define __APP_SCREEN_H

#ifdef __cplusplus
extern "C" {
#endif

void app_screen_init(void);
void app_screen_poll(void);
void app_screen_auto_scale(void);
void app_screen_draw(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SCREEN_H */
