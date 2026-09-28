/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef AUDIO_JITTER_BUFFER_H
#define AUDIO_JITTER_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "rtsp_rtp_config.h"

#define JITTER_BUFFER_SIZE 10  // Number of audio frames to buffer
#define AUDIO_FRAME_SIZE RTP_AUDIO_BUF_SIZE
#define RTP_HEADER_SIZE 12  // Size of header/metadata area (matches RTP header size)

// Simplified slot structure - FIFO only
typedef struct {
    union {
        uint8_t raw_header[RTP_HEADER_SIZE];
        struct {
            uint16_t payload_len;
        } metadata;
    } header;
    uint8_t audio_data[AUDIO_FRAME_SIZE];
} Audio_frame_slot_t;

typedef struct {
    uint32_t write_index;           // Next slot to write to
    uint32_t read_index;            // Next slot to read from
    uint32_t frames_in_buffer;      // Number of frames currently in buffer

    Audio_frame_slot_t slots[JITTER_BUFFER_SIZE];
    Audio_frame_slot_t silence_slot;

    uint32_t packets_dropped;
    uint32_t packets_received;
    uint32_t packets_played;
    uint32_t silence_played;
} Jitter_buffer_t;



// API functions
void jitter_buffer_init(Jitter_buffer_t *jb);
void jitter_buffer_deinit(Jitter_buffer_t *jb);
Audio_frame_slot_t* jitter_buffer_get_rx_slot(Jitter_buffer_t *jb);
bool jitter_buffer_commit_rx(Jitter_buffer_t *jb, Audio_frame_slot_t *slot, uint16_t payload_len);
uint8_t* jitter_buffer_get(Jitter_buffer_t *jb, uint32_t *len);
void jitter_buffer_reset(Jitter_buffer_t *jb);
void jitter_buffer_print_stats(Jitter_buffer_t *jb);

#endif /* AUDIO_JITTER_BUFFER_H */
