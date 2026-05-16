#pragma once

// Halberd v5 carrier — XIAO ESP32-C5 (U7) pin map.
// Source of truth: hw/pcb/diginode-v5/rev2.kicad_sch and
// hw/pcb/docs/kicad-tutorial-v5.md Appendix B.
//
// Each XIAO D-pin label maps to a fixed ESP32-C5 GPIO via the Seeed XIAO C5
// datasheet. Net names are written from the S3's perspective (UART crossover
// applies — see C5_LINK_* below).

// ── S3 link UART (UART0 default pads) ──────────────────────────────────────
// C5_LINK_TX is the wire on which S3 transmits → lands on C5's RX (GPIO12).
// C5_LINK_RX is the wire on which C5 transmits → lands on S3's RX.
#define HALBERD_C5_LINK_TX_GPIO    11  // D6, net C5_LINK_RX (C5 TX → S3 RX)
#define HALBERD_C5_LINK_RX_GPIO    12  // D7, net C5_LINK_TX (S3 TX → C5 RX)
#define HALBERD_C5_LINK_UART_NUM   0
#define HALBERD_C5_LINK_BAUD       921600

// ── GPS UART (UART1) ───────────────────────────────────────────────────────
// ATGM336H breakout, 9600 8N1. PPS pin is exposed on the GPS connector but
// not wired to the C5 in v5 (would need a hand-mod).
#define HALBERD_C5_GPS_RX_GPIO     8   // D8, net GPS_TO_MCU  (GPS TX → C5 RX)
#define HALBERD_C5_GPS_TX_GPIO     9   // D9, net GPS_FROM_MCU (C5 TX → GPS RX)
#define HALBERD_C5_GPS_UART_NUM    1
#define HALBERD_C5_GPS_BAUD        9600

// ── Expansion I²C bus (Qwiic + J_EXP, NO pull-ups on the carrier) ────────
// Carrier intentionally omits SDA/SCL pull-ups — relies on the plugged-in
// Qwiic module's own pulls when present. exp.c enables the C5's internal
// ~45 kΩ pulls as a fallback for J_EXP raw-wire setups where no Qwiic
// module is on the bus to supply them.
#define HALBERD_C5_EXP_SDA_GPIO    7   // D3
#define HALBERD_C5_EXP_SCL_GPIO    23  // D4

// ── Expansion GPIOs (J_EXP, 5 raw pins) ────────────────────────────────────
#define HALBERD_C5_EXP_GPIO0       1   // D0
#define HALBERD_C5_EXP_GPIO1       0   // D1
#define HALBERD_C5_EXP_GPIO2       25  // D2
#define HALBERD_C5_EXP_GPIO3       24  // D5
#define HALBERD_C5_EXP_GPIO4       18  // D10
