/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "audio_jitter_buf.h"
#include "rtsp_rtp_config.h"
#include <string.h>
#include <assert.h>
#include "fsl_debug_console.h"

#define JB_PRINT PRINTF

#define JB_ENTER_CRITICAL UBaseType_t uxSavedInterruptStatus;\
                          uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR()
#define JB_EXIT_CRITICAL  taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus)

// #define JB_ENTER_CRITICAL
// #define JB_EXIT_CRITICAL

void jitter_buffer_init(Jitter_buffer_t *jb)
{
    if (jb == NULL) {
        return;
    }

    memset(jb, 0, sizeof(Jitter_buffer_t));
    jb->write_index = 0;
    jb->read_index = 0;
    jb->frames_in_buffer = 0;

    // Initialize silence slot
    memset(jb->silence_slot.audio_data, 0, AUDIO_FRAME_SIZE);

    JB_PRINT("JB: Initialized simple FIFO mode (ISR-safe, no mutex)\r\n");
}

void jitter_buffer_deinit(Jitter_buffer_t *jb)
{
    if (jb == NULL) return;

    // Reset all state
    jb->write_index = 0;
    jb->read_index = 0;
    jb->frames_in_buffer = 0;
    jb->packets_dropped = 0;
    jb->packets_received = 0;
    jb->packets_played = 0;
    jb->silence_played = 0;

    JB_PRINT("JB: Deinitialized\r\n");
}


Audio_frame_slot_t* jitter_buffer_get_rx_slot(Jitter_buffer_t *jb)
{
    if (jb == NULL) return NULL;

    // Check if buffer is full
    if (jb->frames_in_buffer >= JITTER_BUFFER_SIZE) {
        // Buffer is full - return the previous slot (latest written slot)
        // Calculate previous write index (wrap around if needed)
        uint32_t prev_write_index = (jb->write_index == 0) ?
                                    (JITTER_BUFFER_SIZE - 1) :
                                    (jb->write_index - 1);

        JB_PRINT("JB: Buffer full (frames=%u/%u), reusing slot %u\r\n",
                 jb->frames_in_buffer, JITTER_BUFFER_SIZE, prev_write_index);

        jb->packets_dropped++;

        // Return the previous slot to overwrite it
        return &jb->slots[prev_write_index];
    }

    // Return slot at write_index
    return &jb->slots[jb->write_index];
}

bool jitter_buffer_commit_rx(Jitter_buffer_t *jb,
                            Audio_frame_slot_t *slot,
                            uint16_t payload_len)
{
    if (jb == NULL || slot == NULL) {
        return false;
    }

    // Calculate previous write index for comparison
    uint32_t prev_write_index = (jb->write_index == 0) ?
                                (JITTER_BUFFER_SIZE - 1) :
                                (jb->write_index - 1);

    // Check if this is the current write slot or the previous slot (overflow case)
    bool is_current_slot = (slot == &jb->slots[jb->write_index]);
    bool is_prev_slot = (slot == &jb->slots[prev_write_index]);

    if (!is_current_slot && !is_prev_slot) {
        JB_PRINT("JB: Slot mismatch! Expected index %u or %u\r\n",
                 jb->write_index, prev_write_index);
        return false;
    }

    // Store payload length
    slot->header.metadata.payload_len = payload_len;

    // Only advance write pointer and increment counter if this is a new slot
    // (not an overwrite of the previous slot due to buffer full)
    if (is_current_slot) {
        JB_ENTER_CRITICAL;
        // Advance write index (circular)
        jb->write_index = (jb->write_index + 1) % JITTER_BUFFER_SIZE;
        jb->frames_in_buffer++;
        jb->packets_received++;
        JB_EXIT_CRITICAL;
    } else {
        // Overwriting previous slot - just update packet received count
        JB_ENTER_CRITICAL;
        jb->packets_received++;
        JB_EXIT_CRITICAL;
    }

    return true;
}

uint8_t* jitter_buffer_get(Jitter_buffer_t *jb, uint32_t *len)
{
    if (jb == NULL || len == NULL) {
        return NULL;
    }

    // Check if there are frames in buffer
    if (jb->frames_in_buffer == 0) {
        // No frames ready - return silence
        *len = AUDIO_FRAME_SIZE;
        jb->silence_played++;
        return jb->silence_slot.audio_data;
    }

    // Get frame from read_index
    Audio_frame_slot_t *slot = &jb->slots[jb->read_index];
    uint8_t *audio_ptr = slot->audio_data;
    *len = slot->header.metadata.payload_len;

    // Advance read_index and update counter
    JB_ENTER_CRITICAL;
    jb->read_index = (jb->read_index + 1) % JITTER_BUFFER_SIZE;
    jb->frames_in_buffer--;
    jb->packets_played++;
    JB_EXIT_CRITICAL;

    return audio_ptr;
}

void jitter_buffer_reset(Jitter_buffer_t *jb)
{
    if (jb == NULL) return;

    jb->write_index = 0;
    jb->read_index = 0;
    jb->frames_in_buffer = 0;
    jb->packets_dropped = 0;

    JB_PRINT("JB: Reset\r\n");
}

void jitter_buffer_print_stats(Jitter_buffer_t *jb)
{
    if (jb == NULL) return;

    JB_PRINT("JB Stats: Dropped:%u, Received=%u, Played=%u, Silence=%u\r\n",
             jb->packets_dropped, jb->packets_received, jb->packets_played, jb->silence_played);
    JB_PRINT("         Frames=%u/%u (W=%u, R=%u)\r\n",
             jb->frames_in_buffer, JITTER_BUFFER_SIZE,
             jb->write_index, jb->read_index);
}
