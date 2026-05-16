#pragma once

// Halberd S3 ↔ C5 link codec — COBS-framed, CRC-16-protected frames.
//
// Frame layout (pre-encoding):
//   [type:1][seq:1][len:1][payload:len][crc16_le:2]
//
// On the wire each frame is COBS-encoded and terminated by a single 0x00
// byte, which is the only zero byte ever present in the stream. The
// receiver scans for 0x00 to find frame boundaries, COBS-decodes the
// preceding bytes, then verifies the CRC.
//
// CRC variant: CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflect,
// xorout 0) over [type][seq][len][payload]. Standard XMODEM-style CRC.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_MAX_PAYLOAD  255u
#define LINK_HEADER_LEN     3u
#define LINK_FOOTER_LEN     2u
#define LINK_MAX_RAW        (LINK_HEADER_LEN + LINK_MAX_PAYLOAD + LINK_FOOTER_LEN)

// COBS adds at most ceil(N/254) + 1 bytes of overhead, then we append the
// 0x00 delimiter on the wire. Worst case: N + N/254 + 2 ≈ 263 for N=260.
#define LINK_MAX_ENCODED    (LINK_MAX_RAW + (LINK_MAX_RAW / 254u) + 2u)

// Decoder counters — wrap freely; consumers compare deltas, not absolutes.
typedef struct {
    uint32_t ok;
    uint32_t bad_crc;
    uint32_t bad_length;
    uint32_t short_frame;
    uint32_t overflow;
} link_decoder_stats_t;

typedef struct {
    uint8_t  buf[LINK_MAX_ENCODED];
    size_t   fill;
    bool     overflow;
    link_decoder_stats_t stats;
} link_decoder_t;

typedef void (*link_frame_cb)(void *ctx, uint8_t type, uint8_t seq,
                              const uint8_t *payload, size_t payload_len);

// CRC-16/CCITT-FALSE. Exposed for tests; the encoder/decoder call this
// internally.
uint16_t link_crc16(const uint8_t *data, size_t len);

// Encode a frame into `out`. Returns total bytes written (including the
// trailing 0x00 delimiter), or 0 on error (oversized payload, undersized
// output buffer).
size_t link_encode(uint8_t type, uint8_t seq,
                   const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_cap);

// Streaming decoder. Feed received bytes; the callback fires once per
// validated frame. Malformed frames are silently dropped — their cause is
// counted in d->stats so callers can spot misbehaviour without halting.
void link_decoder_init(link_decoder_t *d);
void link_decoder_feed(link_decoder_t *d,
                       const uint8_t *bytes, size_t n,
                       link_frame_cb cb, void *ctx);

#ifdef __cplusplus
}
#endif
