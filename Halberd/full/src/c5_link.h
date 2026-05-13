#pragma once

// S3-side companion of the Halberd C5 link layer.
//
// Mirrors Halberd/c5/main/link.h. UART2 (GPIO43 TX / GPIO44 RX @ 921600 baud)
// is dedicated to the C5 on the v5 carrier; on v4 these same pins ran the
// GPS UART, which now lives on the C5. Stage 2 wires the codec and the
// FreeRTOS task; stage 3 will swap the GPS init for c5LinkInit() and route
// GPS data through the link instead.

#ifdef __cplusplus
extern "C" {
#endif

void c5LinkInit(void);
void c5LinkSendPing(void);
void c5LinkSendStatus(void);
void c5LinkLogStats(void);

#ifdef __cplusplus
}
#endif
