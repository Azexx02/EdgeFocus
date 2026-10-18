#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool EdgeFocus_FanBoard_Init(uint32_t now_ms);
uint32_t EdgeFocus_FanBoard_InitError(void);
void EdgeFocus_FanBoard_Poll(uint32_t now_ms);
void EdgeFocus_FanBoard_OnRx(const uint8_t *data, size_t size, uint32_t now_ms);
void EdgeFocus_FanBoard_OnTransportReady(uint32_t now_ms);
void EdgeFocus_FanBoard_OnTransportLost(void);
void EdgeFocus_FanBoard_ForceSafe(void);
void EdgeFocus_FanBoard_Shutdown(void);

/* Implemented by the C/OpenAMP transport layer. */
bool EdgeFocus_FanBoard_Transmit(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
