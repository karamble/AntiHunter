#include "link_codec.h"

#include <string.h>

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
uint16_t link_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// COBS encode `in` (in_len bytes, can contain zeros) into `out`. Does NOT
// append the trailing 0x00 delimiter — callers append it themselves.
// Returns the COBS-encoded length.
static size_t cobs_encode(const uint8_t *in, size_t in_len, uint8_t *out) {
    // Special case: empty input encodes to a single 0x01 code byte.
    if (in_len == 0) {
        out[0] = 0x01;
        return 1;
    }

    size_t code_idx = 0;   // index of the current code byte in `out`
    size_t out_idx  = 1;   // next byte to write after the code byte
    uint8_t code    = 1;   // distance to next zero (or 0xFF if no zero yet)

    out[code_idx] = 0;

    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == 0) {
            out[code_idx] = code;
            code_idx = out_idx++;
            out[code_idx] = 0;
            code = 1;
        } else {
            out[out_idx++] = in[i];
            code++;
            if (code == 0xFF) {
                out[code_idx] = code;
                code_idx = out_idx++;
                out[code_idx] = 0;
                code = 1;
            }
        }
    }
    out[code_idx] = code;
    return out_idx;
}

// COBS decode a single frame (no trailing delimiter expected in `in`).
// Returns decoded length, or 0 on malformed input.
static size_t cobs_decode(const uint8_t *in, size_t in_len, uint8_t *out) {
    if (in_len == 0) {
        return 0;
    }
    size_t in_idx  = 0;
    size_t out_idx = 0;
    while (in_idx < in_len) {
        uint8_t code = in[in_idx++];
        if (code == 0) {
            return 0;  // unexpected zero inside encoded body
        }
        for (uint8_t i = 1; i < code; i++) {
            if (in_idx >= in_len) {
                return 0;  // truncated
            }
            out[out_idx++] = in[in_idx++];
        }
        // Insert an implicit zero unless this was a "no zero in 254 bytes"
        // run (code == 0xFF) or we just consumed the final byte.
        if (code < 0xFF && in_idx < in_len) {
            out[out_idx++] = 0x00;
        }
    }
    return out_idx;
}

size_t link_encode(uint8_t type, uint8_t seq,
                   const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_cap) {
    if (payload_len > LINK_MAX_PAYLOAD) {
        return 0;
    }
    if (out_cap < LINK_MAX_ENCODED) {
        return 0;
    }

    uint8_t raw[LINK_MAX_RAW];
    raw[0] = type;
    raw[1] = seq;
    raw[2] = (uint8_t)payload_len;
    if (payload_len > 0) {
        memcpy(&raw[LINK_HEADER_LEN], payload, payload_len);
    }
    const size_t crc_offset = LINK_HEADER_LEN + payload_len;
    const uint16_t crc = link_crc16(raw, crc_offset);
    raw[crc_offset]     = (uint8_t)(crc & 0xFFu);
    raw[crc_offset + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    const size_t raw_len = crc_offset + LINK_FOOTER_LEN;

    const size_t enc_len = cobs_encode(raw, raw_len, out);
    out[enc_len] = 0x00;
    return enc_len + 1;
}

void link_decoder_init(link_decoder_t *d) {
    d->fill = 0;
    d->overflow = false;
    memset(&d->stats, 0, sizeof(d->stats));
}

void link_decoder_feed(link_decoder_t *d,
                       const uint8_t *bytes, size_t n,
                       link_frame_cb cb, void *ctx) {
    for (size_t i = 0; i < n; i++) {
        const uint8_t b = bytes[i];

        if (b != 0x00) {
            if (d->fill >= sizeof(d->buf)) {
                d->overflow = true;  // accumulate; we'll drop at delimiter
            } else {
                d->buf[d->fill++] = b;
            }
            continue;
        }

        // 0x00 → end of frame. Decide whether we have something to decode.
        if (d->overflow) {
            d->stats.overflow++;
            d->fill = 0;
            d->overflow = false;
            continue;
        }
        if (d->fill == 0) {
            // Idle / resync byte — common on startup. Don't count as error.
            continue;
        }

        uint8_t decoded[LINK_MAX_RAW];
        const size_t decoded_len = cobs_decode(d->buf, d->fill, decoded);
        d->fill = 0;

        if (decoded_len < LINK_HEADER_LEN + LINK_FOOTER_LEN) {
            d->stats.short_frame++;
            continue;
        }

        const uint8_t type     = decoded[0];
        const uint8_t seq      = decoded[1];
        const uint8_t pay_len  = decoded[2];

        if (decoded_len != (size_t)LINK_HEADER_LEN + pay_len + LINK_FOOTER_LEN) {
            d->stats.bad_length++;
            continue;
        }

        const size_t crc_offset = LINK_HEADER_LEN + pay_len;
        const uint16_t got_crc =
            (uint16_t)decoded[crc_offset] |
            ((uint16_t)decoded[crc_offset + 1] << 8);
        const uint16_t want_crc = link_crc16(decoded, crc_offset);
        if (got_crc != want_crc) {
            d->stats.bad_crc++;
            continue;
        }

        d->stats.ok++;
        if (cb) {
            cb(ctx, type, seq, &decoded[LINK_HEADER_LEN], pay_len);
        }
    }
}
