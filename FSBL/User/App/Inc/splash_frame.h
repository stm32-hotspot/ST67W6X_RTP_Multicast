#ifndef SPLASH_FRAME_H
#define SPLASH_FRAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

extern uint8_t splash_frame[];
extern const uint32_t splash_frame_len;

void SplashFrame_Init(void);
bool SplashFrame_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* SPLASH_FRAME_H */