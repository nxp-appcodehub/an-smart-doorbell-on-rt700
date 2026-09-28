/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RTSP_RTP_CONFIG_H_
#define RTSP_RTP_CONFIG_H_
#include "hal_camera_shared.h"

/* RTSP server listen port. */
#define RTSP_SERVER_PORT 8554
#define RTSP_TASKS_STACK_SIZE 512


/* RTSP server support video only, video + one way audio(server->client) and
 * video + two ways audio. */
#define	RTSP_SERVER_WORKING_MODE_VIDEO_ONLY 0
#define RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO 1


#define RTSP_SERVER_WORKING_MODE RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO

/*Jpeg frame buffer, it should be the maximum length of jpeg frame received from camera. unit: byte */
#define RTP_JPEG_FRAME_WIDTH TARGET_CAMERA0_WIDTH
#define RTP_JPEG_FRAME_HEIGHT TARGET_CAMERA0_HEIGHT
#define RTP_JPEG_FRAME_FPS TARGET_CAMERA_FPS
/*We believe that this is big enough to put a Jpeg frame. */
#define RTP_JPEG_BUF_SIZE (RTP_JPEG_FRAME_WIDTH*RTP_JPEG_FRAME_HEIGHT * 50/100)

/*for video payload, we only support JPEG (raw data can be YUV422 or YUV420)*/
#define RTP_VIDEO_PAYLOAD_TYPE 26
#define RTP_VIDEO_CODEC "JPEG/90000"

/*if the JPEG raw data is YUV422, set to 0, for YUV420 set to 1*/
#define RTP_JPEG_FRAME_TYPE 0

/*for audio payload, we support G711u(0), G711a(8) and dynamic types: L8 and L16*/
#define RTP_AUDIO_PAYLOAD_TYPE 96


/*For G711u and G711a, must be 8k sample rate, 8bits and 1 channel */
#if (RTP_AUDIO_PAYLOAD_TYPE == 0 || RTP_AUDIO_PAYLOAD_TYPE == 8)
/*Sample rate, data width, channel number*/
#define RTP_AUDIO_SAMPLE_RATE 8000
#define RTP_AUDIO_SAMPLE_WIDTH 8 /*bits of audio data width: 8, 16, 24 ,32*/
#define RTP_AUDIO_CHANNEL_NUM 1
#if (RTP_AUDIO_PAYLOAD_TYPE == 0)
#define RTP_AUDIO_CODEC "PCMU/%d/%d"
#else
#define RTP_AUDIO_CODEC "PCMA/%d/%d"
#endif
#elif (RTP_AUDIO_PAYLOAD_TYPE >= 96)
/*Sample rate, data width, channel number*/
#define RTP_AUDIO_SAMPLE_RATE 8000
#define RTP_AUDIO_SAMPLE_WIDTH 16 /*bits of audio data width: 8, 16, 24 ,32*/
#define RTP_AUDIO_CHANNEL_NUM 2
#define RTP_AUDIO_CODEC "L%d/%d/%d"
#else
#error "unsupported RTP payload type!\r\n"
#endif


#define RTP_AUDIO_CROP_LENGTH 20  /*Audio data interval, unit: ms */
#define RTP_AUDIO_FRAME_1S_LENGTH (RTP_AUDIO_SAMPLE_RATE * RTP_AUDIO_SAMPLE_WIDTH/8 * RTP_AUDIO_CHANNEL_NUM)
#define RTP_AUDIO_BUF_SIZE (RTP_AUDIO_FRAME_1S_LENGTH/1000 * RTP_AUDIO_CROP_LENGTH)


#endif /* RTSP_RTP_CONFIG_H_ */
