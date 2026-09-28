/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <rtsp_rtp_server.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "FreeRTOS.h"
#include "task.h"
#include "portable.h"
#include "fsl_cache.h"

#include "app_msg.h"
#include "rtsp_rtp_config.h"
#include "rpmsg_lite.h"

#include "audio_jitter_buf.h"

#define RTSP_STATIC_DATA_TEST 0

#if RTSP_STATIC_DATA_TEST
#include <jpeg_yuv422.h>
#include <hello_pcm_8k16b2ch_400ms.h>
#define RTSP_STATIC_IMG_DATA yuv422_jpeg_image
#define RTSP_STATIC_AUD_DATA hello_pcm_8k16b2ch_400ms_wav

/*make sure we are using YUV 422*/
#undef RTP_JPEG_FRAME_TYPE
#define RTP_JPEG_FRAME_TYPE 0
#undef RTP_JPEG_FRAME_HEIGHT
#define RTP_JPEG_FRAME_HEIGHT 240
#undef RTP_JPEG_FRAME_WIDTH
#define RTP_JPEG_FRAME_WIDTH 320

#undef RTP_AUDIO_PAYLOAD_TYPE
#define RTP_AUDIO_PAYLOAD_TYPE 96
#undef RTP_AUDIO_SAMPLE_WIDTH
#define RTP_AUDIO_SAMPLE_WIDTH 16
#undef RTP_AUDIO_CHANNEL_NUM
#define RTP_AUDIO_CHANNEL_NUM 2
#undef RTP_AUDIO_SAMPLE_RATE
#define RTP_AUDIO_SAMPLE_RATE 8000

/*static test only support video and audio mode*/
#undef RTSP_SERVER_WORKING_MODE
#define RTSP_SERVER_WORKING_MODE RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO
#endif  //end of if RTSP_STATIC_DATA_TEST


#define RTSP_PRINT PRINTF

// ==== Configurable Strings ====
#define RTSP_SESSION_RECV_BUF_SIZE 512


// ==== RTP/RTSP Constants and Field IDs ====
#define RTP_HEADER_SIZE         12
#define JPEG_HEADER_SIZE        8
#define RTSP_MAX_SESSION_NUM    4

/*for YUV422, set to 0, for YUV420 set to 1*/
#define RTSP_JPEG_TYPE_YUV420   1
#define RTSP_JPEG_TYPE_YUV422   0

/* Use 0 to compatible with GStreamer. */
#define JPEG_QTABLE             0  // Use predefined table index (no custom table)
#define RTP_JPEG_FRAGMENT_SIZE  1400  // Max payload size per RTP packet (safe under MTU)
#define RTP_VIDEO_SEND_BUF_SIZE (RTP_HEADER_SIZE + JPEG_HEADER_SIZE + RTP_JPEG_FRAGMENT_SIZE)
/*consider IP header: max 40 bytes, UDP header: 8 bytes*/
#if RTP_VIDEO_SEND_BUF_SIZE > 1450
#error "RTP packet size is too large!"
#endif

/* No fragment for audio packet. */
#define RTP_AUDIO_SEND_BUF_SIZE  (RTP_AUDIO_BUF_SIZE + RTP_HEADER_SIZE)
#if RTP_AUDIO_SEND_BUF_SIZE > 1450
#error "RTP audio buffer size is too large!"
#endif

#define RTP_SSRC_DEFAULT        0x12345678

/* rtp_task waits indefinitely for the next frame, but the wait is sliced so a
 * teardown (is_active cleared) is detected within one slice instead of blocking
 * forever. Using portMAX_DELAY directly would never re-check is_active. */
#define RTP_SEM_WAIT_SLICE_MS   1000u





#define STR_RTSP_SERVER_PORT_VIDEO ";server_port=45678-45679\r\n"
#define STR_RTSP_SERVER_PORT_AUDIO ";server_port=23456-23457\r\n"
#define STR_RTSP_SERVER_PORT_AUDIO_BACK ";server_port=34566-34567\r\n"
#define STR_RTSP_SERVER_SESSION_ID "Session: SID-ABCDEF\r\n"
#define STR_RTSP_SERVER_AUDIO_BACK_REQUIRE "www.onvif.org/ver20/backchannel"




#define STR_OK_RESPONSE "RTSP/1.0 200 OK\r\nCSeq: %d\r\n"
#define STR_BAD_REQUEST "RTSP/1.0 400 Bad Request\r\nCSeq: %d\r\n\r\n"
#define STR_INVALID_STATE "RTSP/1.0 455 Method Not Valid in This State\r\nCSeq: %d\r\n\r\n"
#define STR_UNSUPPORTED_RESPONSE "RTSP/1.0 551 Option not supported\r\n"\
								 "CSeq: %d\r\n"\
								 "Unsupported: %s\r\n"

#define STR_OPTIONS_RESPONSE "RTSP/1.0 200 OK\r\nCSeq: %d\r\nPublic: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n\r\n"

/*user need input: seq,strIP,port,sdpLen,Strsdp */
#define STR_DESCRIBE_RESPONSE "RTSP/1.0 200 OK\r\n" \
	                          "CSeq: %d\r\n" \
							  "Content-Base: rtsp://%s:%d/\r\n"\
							  "Content-Type: application/sdp\r\n"\
							  "Content-Length: %d\r\n\r\n"\
							  "%s"

#if RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_ONLY
#define STR_SDP         "v=0\r\n"\
						"o=- 0 0 IN IP4 %s\r\n"\
						"s=RTSP Server\r\n"\
						"t=0 0\r\n"\
						"m=video 0 RTP/AVP %d\r\n"\
						"a=rtpmap:%d %s\r\n"\
						"a=control:trackID=0\r\n"\
                        "a=recvonly\r\n"\
						"\r\n"

#elif RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO
#define STR_SDP         "v=0\r\n"\
						"o=- 0 0 IN IP4 %s\r\n"\
						"s=RTSP Server\r\n"\
						"t=0 0\r\n"\
						"m=video 0 RTP/AVP %d\r\n"\
						"a=rtpmap:%d %s\r\n"\
						"a=control:trackID=0\r\n"\
                        "a=recvonly\r\n"\
						"m=audio 0 RTP/AVP %d\r\n"\
						"a=rtpmap:%d %s\r\n"\
						"a=ptime:%d\r\n"\
						"a=control:trackID=1\r\n"\
                        "a=recvonly\r\n"

						//need added addtional if audio back channel is not required by the client side
                        //"\r\n"

#define STR_SDP_AUDIO_BACK         "m=audio 0 RTP/AVP %d\r\n"\
                                    "a=rtpmap:%d %s\r\n"\
                                    "a=control:trackID=2\r\n"\
                                    "a=sendonly\r\n"\
                                    "\r\n"
#endif



typedef enum {
	RTSP_UNINIT = 0,
    RTSP_INIT,
    RTSP_READY,
    RTSP_PLAYING
} Rtsp_session_state_t;


typedef enum{
	MEDIA_TYPE_VIDEO,  //from server to client
	MEDIA_TYPE_AUDIO,  //from server to client
	MEDIA_TYPE_AUDIO_BACK, //from client to server
}Rtsp_media_type_t;


typedef struct Stream_pdu_{
	uint32_t len;
	uint32_t max_len;
	uint8_t reserve[3];
	/* This data must be put in the end of */
	uint8_t* data;
} Stream_pdu_t;

typedef struct {
	/*Socket ID for this stream. */
    int rtp_sock;

    /*time stamp of this stream, init value should be random.
     *for video, timestamp can use OS tick related.
     *for audio, timestamp should always increase by a constant.
     */
    uint32_t timestamp;

    /*seq number of this stream, init value should be random.
     * for each rtp packet, it should increase by 1.
     */
    uint16_t seq;

    /*in callback function, video/audio frame sender may send frames faster than
     * RTP task can receive. if current sending has not complete, discard one
     * received.
     */
    uint8_t send_pending;

	/*Timestamp increase unit for each packet, depend on payload type and sample rate.*/
	uint32_t ts_unit;

	/*send/receive buffer for rtp packet assembly. */
    uint8_t *rtp_buffer;

    /*Peer address*/
    struct sockaddr_in client_addr;

    /*Semaphore used for buffer accessing synchronization during data receiving. */
    SemaphoreHandle_t sem;

    /* Data receiving buffer, used for data receiving from other task. */
    Stream_pdu_t *rx_pdu;

    /*Whether this stream is active or not. */
    bool is_active;

    /*Media type of this stream. */
    Rtsp_media_type_t media_type;

    /* Jitter buffer for audio backchannel */
    Jitter_buffer_t *jitter_buffer;


    /* Number of frames rx/tx for this stream */
    uint32_t frame_count;

#if RTSP_STATIC_DATA_TEST
    /* Timer for stream simulation purpose.*/
    TimerHandle_t timer;
#endif
} Media_stream_t;

typedef struct {

	/*RTSP session socket ID. */
    int sess_sock;
    /* buffer pointer for sending response. */
    char *response;

    Media_stream_t video;
    Media_stream_t audio;
    Media_stream_t audio_back;

    /* OS task handler for this session.*/
    TaskHandle_t handler;

    //save latest sequence number
    uint16_t lastSeq;

    Rtsp_session_state_t state;

} Rtsp_session_t;


typedef struct {

	/*save all the session contexts on the rtsp server. */
	Rtsp_session_t sessions[RTSP_MAX_SESSION_NUM];

	/*rtsp server task handler. */
	TaskHandle_t serverH;

	/*Socket ID rtsp server is listening. */
	int listen_fd;
} Rtsp_context_t;

/*Standard RTP header and Jpeg frame subheader for a RTP packet. */
typedef struct {
   uint8_t  vpxcc;
   uint8_t  mpt;
   uint16_t seq;
   uint32_t timestamp;
   uint32_t ssrc;
} PACK_STRUCT_STRUCT Rtp_header_t;

typedef struct {
   uint8_t  type_specific;
   uint8_t  fragment_offset1;
   uint8_t  fragment_offset2;
   uint8_t  fragment_offset3;
   uint8_t  type;
   uint8_t  q;
   uint8_t  width;
   uint8_t  height;
} PACK_STRUCT_STRUCT Jpeg_header_t ;

extern struct rpmsg_lite_instance *my_rpmsg;
extern void virtCamSendReq(struct rpmsg_lite_endpoint* ep, virtual_usb_cam_msg_type_e msgType, void** buf, uint32_t *len);
extern void audio_task_notify(uint8_t cmd, msg_t* para);
extern void audio_start_play(void);
struct rpmsg_lite_endpoint *rtsp_ept = NULL;

/* Static buffer for jpeg frame and audio frame.  They are all in cached area. */
static uint8_t s_jpegFrameBuffer[RTP_JPEG_BUF_SIZE] __attribute__((section(".noinit.$prv_sh_mem"), aligned(32)));
static uint8_t s_audioFrameBuffer[RTP_AUDIO_BUF_SIZE] __attribute__((aligned(4)));

/*rtsp context include rtsp server, rtsp sessions, rtp tasks and streams. */
static Rtsp_context_t g_rtsp_ctx = {.serverH = NULL,.listen_fd = -1};
static void send_rtp_packet(Media_stream_t *stream,
							Rtp_header_t* rtp_header,
							Jpeg_header_t* jpeg_header);
static bool validate_rtp_header(Rtp_header_t *rtp_header,
                                uint16_t total_bytes,
                                uint16_t *rtp_seq,
                                uint32_t *rtp_timestamp,
                                uint16_t *payload_len);

static Jitter_buffer_t g_audio_back_jitter_buffer;
static bool g_audio_back_play_start = false;



static void rtsp_send(int s, const void *data, size_t size, int flags)
{
	PRINTF("%s",data);
	send(s, data, size, flags);
}



static void safe_close_socket(int *sock_fd) {
    if (*sock_fd >= 0) {
        close(*sock_fd);
        *sock_fd = -1;
    }
}

static int parse_client_port_from_transport(const char *header) {
    const char *p = strstr(header, "client_port=");
    if (!p) return -1;
    return atoi(p + strlen("client_port="));
}

// TX callback (Audio back channel socket rx --> Jitter buffer --> EDMA --> Codec)
// Audio task will call this callback to get next available audio frame from jitter buffer
static void audio_tx_callback(uint8_t* buf, uint32_t len, void* para)
{
    Media_stream_t *stream = (Media_stream_t *)para;

    if (stream == NULL || stream->jitter_buffer == NULL || buf == NULL) {
        return;
    }

    uint32_t audio_len = 0;
    uint8_t *audio_data = jitter_buffer_get(stream->jitter_buffer, &audio_len);

    /* This callback runs in the SAI TX-DMA ISR context. Never assert() here:
     * a remote client can legitimately send a back-channel RTP payload whose
     * size differs from the codec buffer size (padding, different ptime,
     * comfort noise, truncated datagram, etc.). Asserting in an ISR would halt
     * the core and freeze the whole system. Handle the mismatch gracefully by
     * copying only the bytes we have and padding the remainder with silence. */
    if (audio_data == NULL || audio_len == 0)
    {
        /* No data available - output silence so the codec keeps running. */
        memset(buf, 0, len);
        return;
    }

    /* Clamp the number of bytes we consume to the smaller of the two buffers so
     * we never read past the jitter-buffer slot or write past the codec buffer. */
    uint32_t copy_len = (audio_len < len) ? audio_len : len;

    // Byte swap for codec

    uint16_t* dst = (uint16_t*)buf;
    uint16_t* src = (uint16_t*)audio_data;
    for (uint32_t i = 0; i < copy_len/sizeof(uint16_t); i++)
    {
        dst[i] = ntohs(src[i]);
    }

    /* Pad any remaining codec buffer space with silence. */
    if (copy_len < len)
    {
        memset(buf + copy_len, 0, len - copy_len);
    }
}



static void rtp_task_pdu_cb(uint8_t* buf, uint32_t len, void* para)
{
	Media_stream_t *stream = (Media_stream_t*)para;
	Stream_pdu_t* pdu = stream->rx_pdu;

	/*if current frame has not been sent out completely, drop this one.*/
	if (stream->send_pending == 1
		|| len > pdu->max_len)
	{
        RTSP_PRINT("pdu dropped: %d\r\n", pdu->max_len);
		return;
	}

	if (stream->media_type == MEDIA_TYPE_AUDIO)
	{
		stream->timestamp += stream->ts_unit;
		/*if received data is not the format required, here is a good place for conversion. */
#if (RTP_AUDIO_SAMPLE_WIDTH == 16)
		uint16_t* dst = (uint16_t*)pdu->data;
		uint16_t* src = (uint16_t*)buf;
		for (int i = 0;i < len/sizeof(uint16_t); i++)
		{
			dst[i] = htons(src[i]);
            // dst[i] = src[i];
		}
#elif (RTP_AUDIO_SAMPLE_WIDTH == 24)
		uint8_t* dst = (uint8_t*)pdu->data;
		uint8_t* src = (uint8_t*)buf;
		for (int i = 0;i < len; i += 3)
		{
			dst[i] = src[i + 2];
			dst[i + 2] = src[i];
		}
#elif (RTP_AUDIO_SAMPLE_WIDTH == 32)
		uint32_t* dst = (uint32_t*)pdu->data;
		uint32_t* src = (uint32_t*)buf;
		for (int i = 0;i < len/sizeof(uint32_t); i++)
		{
			dst[i] = htonl(src[i]);
		}
#else
		memcpy(pdu->data,buf,len);
#endif
	}else
	{
		/*Use tick count for timestamps. */
		stream->timestamp = xTaskGetTickCount() * stream->ts_unit;
		if (buf != pdu->data)
		{
			memcpy(pdu->data,buf,len);
		}
	}

	pdu->len = len;


	/* Notify RTP task, pdu is ready. */
	stream->send_pending = 1;

	if (__get_IPSR() == 0)
	{
		xSemaphoreGive( stream->sem );
	}else
	{
	    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xSemaphoreGiveFromISR( stream->sem, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}

}


static int32_t rtp_task_rpmsg_ep_cb(void* buf, uint32_t len, uint32_t src, void* para)
{
	virtual_usb_cam_msg_t *msg = (virtual_usb_cam_msg_t *)buf;
	virtual_usb_cam_rsp_msg_t* rsp = &msg->msg_payload.rsp;

	if (msg->msg_type == VIRT_USB_CAM_RSPRGB)
	{
		XCACHE_InvalidateCacheByRange(rsp->rgb_frame_addr,rsp->rgb_frame_size);
		rtp_task_pdu_cb((uint8_t*)rsp->rgb_frame_addr,rsp->rgb_frame_size,para);

	}else if (msg->msg_type == VIRT_USB_CAM_RSPIR)
	{
		XCACHE_InvalidateCacheByRange(rsp->ir_frame_addr,rsp->ir_frame_size);
		rtp_task_pdu_cb((uint8_t*)rsp->ir_frame_addr,rsp->ir_frame_size,para);
	}else
	{

	}

	return RL_RELEASE;

}



static int generate_sdp(char *buffer, size_t max_len, const char *ip_addr,
                 int video_pt, const char *video_codec,
                 int audio_pt, const char *audio_codec, int ptime,bool req_audioback)
{
#if (RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_ONLY)
    return snprintf(buffer, max_len,
        STR_SDP,
        ip_addr,
        video_pt,
		video_pt,
		video_codec);
#elif RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO
    int ret = snprintf(buffer, max_len,
        STR_SDP,
        ip_addr,
        video_pt, video_pt, video_codec,
        audio_pt, audio_pt, audio_codec, ptime);

    assert(ret > 0 && ret < (int)max_len);
    if (!req_audioback)
    {
        ret = snprintf(buffer + ret, max_len - ret,"\r\n");
    }else
    {
        ret = snprintf(buffer + ret, max_len - ret,
        STR_SDP_AUDIO_BACK,
		audio_pt, audio_pt, audio_codec);
    }
    assert(ret > 0 && ret < (int)max_len);
    return ret;
#endif
}


#if RTSP_STATIC_DATA_TEST
static void rtp_timerCallback( TimerHandle_t pxTimer )
{
//	static uint32_t last_frame_counter = 0;
	static uint32_t audio_read_idx = 0;
	assert(pxTimer);
	Media_stream_t *stream = (Media_stream_t *)pvTimerGetTimerID( pxTimer );

	if (stream == NULL)
	{
//		RTSP_PRINT("RTP fps:%d\r\n", (g_FrameCounter - last_frame_counter)/g_FPSTimeoutSeconds);
//		last_frame_counter = g_FrameCounter;

	}else if (stream->media_type == MEDIA_TYPE_VIDEO)
	{
		rtp_task_pdu_cb(RTSP_STATIC_IMG_DATA,
				sizeof(RTSP_STATIC_IMG_DATA),
				stream);

	}else if (stream->media_type == MEDIA_TYPE_AUDIO)
	{
		rtp_task_pdu_cb((uint8_t*)RTSP_STATIC_AUD_DATA + audio_read_idx,
							RTP_AUDIO_BUF_SIZE,
							stream);
		audio_read_idx = (audio_read_idx + RTP_AUDIO_BUF_SIZE) % sizeof(RTSP_STATIC_AUD_DATA);
	}else
	{
		assert(0);
	}

}
#endif


static void rtp_registerPduCallback(Media_stream_t *stream)
{
	assert(stream != NULL);
	if( stream->sem == NULL )
	{
		stream->sem = xSemaphoreCreateBinary();
		RTSP_PRINT("Semaphore created for rtp 0x%p\r\n", stream->sem);
	}

	//allocate memory for PDU
	stream->rx_pdu = pvPortMalloc(sizeof(Stream_pdu_t));
	assert(stream->rx_pdu != NULL);

	if (stream->media_type == MEDIA_TYPE_VIDEO)
	{
		stream->rx_pdu->data = s_jpegFrameBuffer;
		stream->rx_pdu->max_len = sizeof(s_jpegFrameBuffer);
	}else if (stream->media_type == MEDIA_TYPE_AUDIO)
	{
		stream->rx_pdu->data = s_audioFrameBuffer;
		stream->rx_pdu->max_len = sizeof(s_audioFrameBuffer);
	}else // MEDIA_TYPE_AUDIO_BACK
	{
		stream->rx_pdu->data = NULL;
		stream->rx_pdu->max_len = 0;
	}

#if RTSP_STATIC_DATA_TEST
	uint32_t timePeriodMs;
	if (stream->media_type == MEDIA_TYPE_VIDEO)
	{
		timePeriodMs = 1000/RTP_JPEG_FRAME_FPS;
	}else
	{
		timePeriodMs = RTP_AUDIO_CROP_LENGTH;
	}

	/*start a timer to simulate video/audio frame stream. */
	stream->timer = xTimerCreate("PDUTimer",
								pdMS_TO_TICKS(timePeriodMs) , // The timer period in ticks.
	                            pdTRUE,              // The timers will auto-reload themselves when they expire.
	                            (void *)stream,        // reuse for parameter
							    rtp_timerCallback       // callback
	                            );

	assert(xTimerStart( stream->timer, 0 ) == pdPASS);

#else
	if (stream->media_type == MEDIA_TYPE_VIDEO)
	{
		/*Register for jpeg frame receiving callback.*/
		if (my_rpmsg != NULL)
		{
			rtsp_ept = rpmsg_lite_create_ept(my_rpmsg,
							RTSP_EPT_ADDRESS,
							rtp_task_rpmsg_ep_cb,
							stream);

			assert(rtsp_ept != NULL);

			/* it's time to trigger the 1st frame request*/
			void * buf_p[1] = {	stream->rx_pdu->data};
			uint32_t buf_len[1] = {stream->rx_pdu->max_len};

			virtCamSendReq(rtsp_ept, VIRT_USB_CAM_REQRGB,
					buf_p,
					buf_len);
		}
		else
		{
			RTSP_PRINT("RPMSG is not ready!\r\n");
		}

	}else if (stream->media_type == MEDIA_TYPE_AUDIO)
	{
		/*Register for audio pdu receiving callback.*/
        msg_t* msg = pvPortMalloc(sizeof(msg_t));
        msg->msgID = AUDIO_TASK_RTP_REGISTER_RX_CB;
        msg->cb_payload.callback = rtp_task_pdu_cb;
        msg->cb_payload.para = (void*)stream;
        msg->cb_payload.max_buffer_size = stream->rx_pdu->max_len;
        audio_task_notify(AUDIO_TASK_RTP_REGISTER_RX_CB, msg);
	}else // MEDIA_TYPE_AUDIO_BACK
	{
		/* Register TX completion callback for audio backchannel */
		msg_t* tx_msg = pvPortMalloc(sizeof(msg_t));
		tx_msg->msgID = AUDIO_TASK_RTP_REGISTER_TX_CB;
		tx_msg->cb_payload.callback = audio_tx_callback;
		tx_msg->cb_payload.para = (void*)stream;
		tx_msg->cb_payload.max_buffer_size = 0;  // Not used for TX
		audio_task_notify(AUDIO_TASK_RTP_REGISTER_TX_CB, tx_msg);
	}
#endif

}

static void rtp_deregisterPduCallback(Media_stream_t *stream)
{
#if RTSP_STATIC_DATA_TEST
	xTimerStop( stream->timer, 0 );
	xTimerDelete(stream->timer, 0);

#else
	// USB_HostVideoSendMsg2Inst(USB_HOST_VIDEO_TASK_DEREG_FRAME_CB_REQ,
	// 	rtp_task_pdu_cb,
	// 	stream,
	// 	USB_HOST_VIDEO_INST_CAM_0);
    if (stream->media_type == MEDIA_TYPE_VIDEO)
    {

        (void)rpmsg_lite_destroy_ept(my_rpmsg, rtsp_ept);
        rtsp_ept = NULL;
    }else if (stream->media_type == MEDIA_TYPE_AUDIO)
	{
		audio_task_notify(AUDIO_TASK_RTP_DEREGISTER_RX_CB, NULL);
	}else // MEDIA_TYPE_AUDIO_BACK
	{
		audio_task_notify(AUDIO_TASK_RTP_DEREGISTER_TX_CB, NULL);
    }
#endif

    if (stream->sem != NULL)
	{
		vSemaphoreDelete(stream->sem);
		stream->sem = NULL;
	}

	if (stream->rx_pdu != NULL)
	{
		vPortFree(stream->rx_pdu);
		stream->rx_pdu = NULL;
	}
}



// RTP task, created for each stream
static void rtp_task(void *arg)
{
    Media_stream_t *stream = (Media_stream_t *)arg;
    uint16_t port;
    uint32_t rtp_buf_size = 0;

    if (stream->media_type == MEDIA_TYPE_VIDEO)
    {
        stream->ts_unit = atoi(strstr(RTP_VIDEO_CODEC,"/") + 1);
        port = atoi(strstr(STR_RTSP_SERVER_PORT_VIDEO,"=") + 1);
        stream->ts_unit /= configTICK_RATE_HZ;
    }
    else if (stream->media_type == MEDIA_TYPE_AUDIO)
    {
    	stream->ts_unit = RTP_AUDIO_SAMPLE_RATE/1000*RTP_AUDIO_CROP_LENGTH;
    	port = atoi(strstr(STR_RTSP_SERVER_PORT_AUDIO,"=") + 1);

    }else
    {
    	stream->ts_unit = RTP_AUDIO_SAMPLE_RATE/1000*RTP_AUDIO_CROP_LENGTH;
    	port = atoi(strstr(STR_RTSP_SERVER_PORT_AUDIO_BACK,"=") + 1);

        // Initialize jitter buffer for audio backchannel
        stream->jitter_buffer = &g_audio_back_jitter_buffer;
        jitter_buffer_reset(stream->jitter_buffer);

        g_audio_back_play_start = false;
    }

    //allocate rtp packet sending/receiving buffer
    //how much buffer we need allocate for audio backchannel?
    if (stream->media_type == MEDIA_TYPE_VIDEO)
    {
        rtp_buf_size = RTP_VIDEO_SEND_BUF_SIZE;
    }else if (stream->media_type == MEDIA_TYPE_AUDIO)
    {
        rtp_buf_size = RTP_AUDIO_SEND_BUF_SIZE;
    }
    else
    {
        rtp_buf_size = 0;
        stream->rtp_buffer = NULL;
    }

    if (rtp_buf_size > 0)
    {
        stream->rtp_buffer = pvPortMalloc(rtp_buf_size);
        if (stream->rtp_buffer == NULL)
        {
            RTSP_PRINT("can't alloc rtp send buffer!\r\n");
            goto end;
        }
    }

    //it's time to create UDP socket for data transfer
    stream->rtp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (stream->rtp_sock < 0)
    {
    	RTSP_PRINT("can't create rtp socket!\r\n");
    	goto end;
    }

    int reuse_addr = 1;
    if (setsockopt(stream->rtp_sock, SOL_SOCKET, SO_REUSEADDR,
                    &reuse_addr, sizeof(reuse_addr)) < 0)
    {
        // error handling
        RTSP_PRINT("set resue on rtp socket error!\r\n");
    	goto end;
    }

    // Set socket timeout (add this in socket setup section)
    if (stream->media_type == MEDIA_TYPE_AUDIO_BACK)
    {
        struct timeval tv;
        tv.tv_sec = 1;  // 1 second timeout
        tv.tv_usec = 0;
        if (setsockopt(stream->rtp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            RTSP_PRINT("Failed to set socket timeout\r\n");
            goto end;
        }
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(stream->rtp_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    	RTSP_PRINT("Socket bind error!\r\n");
    	goto end;

    }

    stream->send_pending = 0;
    rtp_registerPduCallback(stream);


    /* Select a random start sequence number. */
    stream->frame_count = 0;
    stream->seq = rand() & 0xFFFF;
    stream->timestamp = rand()&0xFFFFFFFFUL;
    Rtp_header_t rtp_header = {
								.vpxcc = 0x80,
								.mpt = (stream->media_type == MEDIA_TYPE_VIDEO) ?
										RTP_VIDEO_PAYLOAD_TYPE:RTP_AUDIO_PAYLOAD_TYPE,
								.seq = htons(0),
								.timestamp = htonl(0),
								.ssrc = htonl(RTP_SSRC_DEFAULT + stream->media_type),

    						 };
    Jpeg_header_t jpeg_header = {
        .type_specific = 0,
        .fragment_offset1 = 0,
        .fragment_offset2 = 0,
        .fragment_offset3 = 0,
        .type = RTP_JPEG_FRAME_TYPE,
        .q = JPEG_QTABLE,
        .width = RTP_JPEG_FRAME_WIDTH >> 3,
        .height = RTP_JPEG_FRAME_HEIGHT >> 3
    };


    while (stream->is_active)
    {
        if (stream->media_type == MEDIA_TYPE_AUDIO_BACK)
        {
            // Get empty slot from jitter buffer
            Audio_frame_slot_t *rx_slot = jitter_buffer_get_rx_slot(stream->jitter_buffer);

audio_back_rx:
            socklen_t client_len = sizeof(stream->client_addr);
            ssize_t bytes_received = recvfrom(stream->rtp_sock,
                                            rx_slot,
                                            sizeof(Audio_frame_slot_t),
                                            0,
                                            (struct sockaddr *)&stream->client_addr,
                                            &client_len);

            if (bytes_received < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    // Timeout - check is_active flag and continue
                    goto audio_back_rx;
                }
                RTSP_PRINT("rtp audio back task rx error: %d\r\n", errno);
                break;
            }
            else if (bytes_received < RTP_HEADER_SIZE)
            {
                // Packet too small, drop it
                RTSP_PRINT("rtp audio backchannel rtp packet too small: %d\r\n", bytes_received);
                goto audio_back_rx;
            }
            else
            {

                // Validate RTP header and extract fields
                uint16_t rtp_seq;
                uint32_t rtp_timestamp;
                uint16_t payload_len;

                if (!validate_rtp_header((Rtp_header_t*)rx_slot->header.raw_header,
                                        bytes_received,
                                        &rtp_seq,
                                        &rtp_timestamp,
                                        &payload_len))
                {
                    // Invalid RTP packet, drop it
                    RTSP_PRINT("rtp audio backchannel invalid rtp packet header\r\n");
                    goto audio_back_rx;
                }

                /* Only commit correctly-sized payloads to the jitter buffer.
                 * The SAI TX-DMA ISR consumes exactly AUDIO_FRAME_SIZE bytes per
                 * frame; a payload of a different size (padding, wrong ptime,
                 * comfort noise, truncated/oversized datagram) must never reach
                 * the codec path. Drop mismatched frames here so the ISR only
                 * ever sees well-formed data. */
                if (payload_len != AUDIO_FRAME_SIZE)
                {
                    RTSP_PRINT("rtp audio backchannel unexpected payload len:%u (expected %u), dropping\r\n",
                               payload_len, (uint32_t)AUDIO_FRAME_SIZE);
                    goto audio_back_rx;
                }

                stream->frame_count++;

                // Commit the received data to jitter buffer
                if (!jitter_buffer_commit_rx(stream->jitter_buffer, rx_slot, payload_len))
                {
                    // Failed to commit (late, duplicate, etc.)
                    RTSP_PRINT("rtp audio backchannel jitter buf commit failed\r\n");
                    break;
                }else
                {

                    if (!g_audio_back_play_start)
                    {
                        //start audio backchannel play
                        audio_start_play();
                        g_audio_back_play_start = true;
                    }

                    // print out jitter buffer status
                    if (stream->frame_count % 100 == 0)
                    {
                        // jitter_buffer_print_stats(stream->jitter_buffer);
                        RTSP_PRINT("rtp %s frame#:%u\r\n",
                        "audioBack",
                        stream->frame_count);
                    }
                }

            }
        }
    	else
    	{
            /* Wait indefinitely for the next frame, but stay responsive to a
             * teardown. A single xSemaphoreTake(portMAX_DELAY) would block
             * forever and never notice stream->is_active being cleared, so we
             * poll the semaphore in short slices and re-check is_active between
             * slices. The task therefore quits within one slice of a teardown. */
            BaseType_t sem_taken = pdFALSE;

            while (stream->is_active)
            {
                if (xSemaphoreTake(stream->sem, pdMS_TO_TICKS(RTP_SEM_WAIT_SLICE_MS)) == pdTRUE)
                {
                    sem_taken = pdTRUE;
                    break;
                }
                /* Slice elapsed with no frame; loop and re-check is_active. */
            }


            /* If a teardown cleared is_active while we were waiting, exit the
             * loop immediately without sending a stale frame. break is used
             * (rather than continue) to leave the outer while directly. */
            if (!stream->is_active)
            {
                break;
            }


            if( sem_taken == pdTRUE )
            {
                // Semaphore was successfully taken
                send_rtp_packet(stream, &rtp_header, &jpeg_header);
                stream->send_pending = 0;
                stream->frame_count++;

                if (stream->media_type == MEDIA_TYPE_VIDEO)
                {
                    /* it's time to ask for next frame*/
                    void * buf_p[1] = {	stream->rx_pdu->data};
                    uint32_t buf_len[1] = {stream->rx_pdu->max_len};

                    virtCamSendReq(rtsp_ept, VIRT_USB_CAM_REQRGB,
                            buf_p,
                            buf_len);
                }

                if (stream->frame_count % 100 == 0)
                {
                    RTSP_PRINT("rtp %s frame#:%u\r\n",
                        stream->media_type == MEDIA_TYPE_VIDEO?"video":"audio",
                        stream->frame_count);
                }
            }


    	}
    }

    // Unified deregistration for all media types
    rtp_deregisterPduCallback(stream);

end:
	if (stream->rtp_buffer){
		vPortFree(stream->rtp_buffer);
		stream->rtp_buffer = NULL;
	}
	RTSP_PRINT("rtp task for stream[%d] end!\r\n",stream->media_type);
    safe_close_socket(&stream->rtp_sock);
    vTaskDelete(NULL);
}


static int handle_options(Rtsp_session_t *session, char* recv_buf, int size)
{
	//todo, check URL

	//get sequence number
	uint16_t seq = session->lastSeq;
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_OPTIONS_RESPONSE, seq);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return 0;

}

static int handle_describe(Rtsp_session_t *session,char* recv_buf, int size)
{
	//todo, check URL

    uint16_t seq = session->lastSeq;
    /*Check required*/
    char* required = NULL;
    bool audio_back_required = false;
    if ((required = strstr(recv_buf,"Require:")) != NULL)
    {
        RTSP_PRINT("%s",required);
#if RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO
    	if (strstr(recv_buf,STR_RTSP_SERVER_AUDIO_BACK_REQUIRE) != NULL)
    	{
    		//audio back is required
            audio_back_required = true;
            RTSP_PRINT("Audio backchannel is required!\r\n");

    	}else
#endif
    	{
    		required = required + strlen("Require:");
    		goto unsupported;
    	}
    }

    memset(recv_buf,0,size);

    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    getsockname(session->sess_sock,	(struct sockaddr*)&addr, &addrlen);
    char* ipStr = inet_ntoa(addr.sin_addr);

    //get sdp lines
    uint16_t sdp_size = 512;
    char *sdp_buf = pvPortMalloc(sdp_size);
    assert(sdp_buf != NULL);
    memset(sdp_buf,0,sdp_size);

    char audio_codec[16];
    memset(audio_codec,0, sizeof(audio_codec));
    snprintf(audio_codec, sizeof(audio_codec),
    		RTP_AUDIO_CODEC,
			RTP_AUDIO_SAMPLE_WIDTH,
			RTP_AUDIO_SAMPLE_RATE,
			RTP_AUDIO_CHANNEL_NUM);

    generate_sdp(sdp_buf,sdp_size,ipStr,
    		RTP_VIDEO_PAYLOAD_TYPE,RTP_VIDEO_CODEC,
			RTP_AUDIO_PAYLOAD_TYPE,audio_codec, RTP_AUDIO_CROP_LENGTH,audio_back_required);

    /*user need input: seq,strIP,port,sdpLen,strIP,vPayloadType,vMapStr,aPayloadType,aMapStr */
    snprintf(recv_buf, size,
    		STR_DESCRIBE_RESPONSE,
			seq, ipStr,RTSP_SERVER_PORT,strlen(sdp_buf),sdp_buf);

    vPortFree(sdp_buf);
    rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
    return 0;

unsupported:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_UNSUPPORTED_RESPONSE, seq, required);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return -1;
}



static int handle_setup(Rtsp_session_t *session, char* recv_buf, int size) {

	//get sequence number
	uint16_t seq = session->lastSeq;
	uint8_t media_type = 0;
	Media_stream_t *stream = NULL;

    // Reject SETUP if session is already in PLAYING state
    if (session->state == RTSP_PLAYING) {
    	goto invalid_state;
    }

    //which stream
    if (strstr(recv_buf,"trackID=0"))
    {
    	stream = &session->video;
    	media_type = MEDIA_TYPE_VIDEO;

    }
#if RTSP_SERVER_WORKING_MODE == RTSP_SERVER_WORKING_MODE_VIDEO_AUDIO
    else if (strstr(recv_buf,"trackID=1"))
    {
    	stream = &session->audio;
    	media_type = MEDIA_TYPE_AUDIO;

    }

    else if ((strstr(recv_buf,"trackID=2")))
    {
    	stream = &session->audio_back;
    	media_type = MEDIA_TYPE_AUDIO_BACK;

    }
#endif
    else
    {
    	//how to do if there is no track info?
    	RTSP_PRINT("No track info or trackID not support in setup!");
    	goto bad_request;

    }

    // Reject duplicate SETUP for the same track
    if (stream->is_active) {
    	goto invalid_state;
    }

    // Extract client RTP port from Transport header
    int port = parse_client_port_from_transport(recv_buf);
    if (port <= 0) {
    	RTSP_PRINT("can't get port!");
        goto bad_request;
    }

    struct sockaddr_in addr;
    socklen_t peerlen = sizeof(addr);

    /*remember the addr and port*/
    getpeername(session->sess_sock,	(struct sockaddr*)&addr, &peerlen);
    addr.sin_port = htons(port);
    stream->client_addr = addr;

    stream->media_type = media_type;
    stream->is_active = true;
    stream->rtp_sock = -1;

    // If session was INIT, move to READY
    if (session->state == RTSP_INIT) {
        session->state = RTSP_READY;
    }

    //get transport line
    char* transport = strstr(recv_buf, "Transport:");
    char* transport_eol = (transport != NULL) ? strstr(transport, "\r\n") : NULL;
    if (transport == NULL || transport_eol == NULL)
    {
        /* Malformed Transport header - reject instead of dereferencing NULL. */
        stream->is_active = false;
        RTSP_PRINT("No or malformed Transport header in setup!");
        goto bad_request;
    }

    uint16_t transport_size = 256;
    char *transport_buf = pvPortMalloc(transport_size);
    if (transport_buf == NULL)
    {
        stream->is_active = false;
        RTSP_PRINT("can't alloc transport buffer!\r\n");
        goto bad_request;
    }
    memset(transport_buf,0,transport_size);

    /* Clamp the copied Transport line to the destination buffer to avoid overflow
     * when a client sends an oversized Transport header. */
    size_t transport_line_len = (size_t)(transport_eol - transport);
    if (transport_line_len > (size_t)(transport_size - 1))
    {
        transport_line_len = transport_size - 1;
    }
    memcpy(transport_buf,transport,transport_line_len);

    snprintf(transport_buf + strlen(transport_buf),
    		transport_size - strlen(transport_buf),
    		media_type == MEDIA_TYPE_VIDEO ? STR_RTSP_SERVER_PORT_VIDEO :
            media_type == MEDIA_TYPE_AUDIO ? STR_RTSP_SERVER_PORT_AUDIO : STR_RTSP_SERVER_PORT_AUDIO_BACK);

    memset(recv_buf,0,size);
    //status line and CSeq line
    snprintf(recv_buf, size, STR_OK_RESPONSE, seq);

    //add server port part
    snprintf(recv_buf + strlen(recv_buf),size - strlen(recv_buf),"%s",transport_buf);

    //Add session number line
    snprintf(recv_buf + strlen(recv_buf),
    		size - strlen(recv_buf),
				STR_RTSP_SERVER_SESSION_ID);

    //Add a blank line
    snprintf(recv_buf + strlen(recv_buf),
    		size - strlen(recv_buf),
				"\r\n");

    vPortFree(transport_buf);
    rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);

    return 0;

invalid_state:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_INVALID_STATE, seq);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
    return -1;
bad_request:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_BAD_REQUEST, seq);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return -2;

}



static int handle_play(Rtsp_session_t *session, char* recv_buf, int size)
{
	//get sequence number
	uint16_t seq = session->lastSeq;

    if (session->state == RTSP_INIT) {
        // Must SETUP before PLAY
        goto invalid_state;

    }

    if (session->state == RTSP_PLAYING) {
        // Already playing - accept redundant PLAY command without restarting
    	goto ok;

    }

    //check session ID
    if (NULL == strstr(recv_buf, STR_RTSP_SERVER_SESSION_ID))
    {
    	goto bad_request;
    }

    session->state = RTSP_PLAYING;

    //try to play all
	 if (session->video.is_active)
	 {
		 xTaskCreate(rtp_task, "rtp_video", RTSP_TASKS_STACK_SIZE, &session->video, RTP_TASK_PRIORITY, NULL);

	 }

	 if (session->audio.is_active)
	 {
		//give audio task a higher priority because it should not be blocked.
		xTaskCreate(rtp_task, "rtp_audio", RTSP_TASKS_STACK_SIZE, &session->audio, RTP_TASK_PRIORITY + 1, NULL);

	 }

	 if (session->audio_back.is_active)
	 {
		 xTaskCreate(rtp_task, "rtp_audioBack", RTSP_TASKS_STACK_SIZE, &session->audio_back, RTP_TASK_PRIORITY + 1, NULL);
	 }


ok:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_OK_RESPONSE, seq);
	snprintf(recv_buf + strlen(recv_buf), size - strlen(recv_buf),STR_RTSP_SERVER_SESSION_ID);
    snprintf(recv_buf + strlen(recv_buf), size - strlen(recv_buf),"\r\n");
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return 0;

invalid_state:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_INVALID_STATE, seq);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return -1;
bad_request:
	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_BAD_REQUEST, seq);
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return -2;
}


static int handle_teardown(Rtsp_session_t *session,char* recv_buf, int size)
{
	uint16_t seq = session->lastSeq;

    session->video.is_active = false;
    session->audio.is_active = false;
    session->audio_back.is_active = false;
    session->state = RTSP_UNINIT;

	memset(recv_buf,0,size);
	snprintf(recv_buf, size, STR_OK_RESPONSE, seq);
	snprintf(recv_buf + strlen(recv_buf), size - strlen(recv_buf),STR_RTSP_SERVER_SESSION_ID);
    snprintf(recv_buf + strlen(recv_buf), size - strlen(recv_buf),"\r\n");
	rtsp_send(session->sess_sock, recv_buf, strlen(recv_buf), 0);
	return 0;
}


// RTSP session task: handles OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN for a single client
static void rtsp_session_task(void *arg) {
    int rx_len;
    Rtsp_session_t* session = (Rtsp_session_t*)arg;
    char rx_buf[RTSP_SESSION_RECV_BUF_SIZE];

    session->response = rx_buf;
    memset(rx_buf,0,sizeof(rx_buf));

    while ((rx_len = recv(session->sess_sock,
    		              rx_buf,
    					  RTSP_SESSION_RECV_BUF_SIZE - 1,
						  0)) > 0)
    {
        rx_buf[rx_len] = 0;

        /* Parse CSeq safely. A malformed request without a "CSeq:" header would
         * otherwise dereference a NULL pointer returned by strstr(). */
        char *cseq_p = strstr(rx_buf, "CSeq:");
        if (cseq_p == NULL)
        {
            memset(rx_buf, 0, RTSP_SESSION_RECV_BUF_SIZE);
            snprintf(rx_buf, RTSP_SESSION_RECV_BUF_SIZE, STR_BAD_REQUEST, session->lastSeq);
            rtsp_send(session->sess_sock, rx_buf, strlen(rx_buf), 0);
            memset(rx_buf, 0, sizeof(rx_buf));
            continue;
        }
        session->lastSeq = atoi(cseq_p + strlen("CSeq:"));


        if (strstr(rx_buf, "OPTIONS"))
        {
            RTSP_PRINT("O\r\n");
            handle_options(session,rx_buf,RTSP_SESSION_RECV_BUF_SIZE);

        }
        else if (strstr(rx_buf, "DESCRIBE"))
        {
            RTSP_PRINT("D\r\n");
            handle_describe(session,rx_buf,RTSP_SESSION_RECV_BUF_SIZE);

        }
        else if (strstr(rx_buf, "SETUP"))
        {
        	RTSP_PRINT("S\r\n");
        	handle_setup(session,rx_buf,RTSP_SESSION_RECV_BUF_SIZE);

        }
        else if (strstr(rx_buf, "PLAY"))
        {
        	RTSP_PRINT("P\r\n");
        	handle_play(session,rx_buf,RTSP_SESSION_RECV_BUF_SIZE);

        }
        else if (strstr(rx_buf, "TEARDOWN")) {
            // Clean up session (stop media, close sockets)
        	RTSP_PRINT("T\r\n");
        	handle_teardown(session,rx_buf,RTSP_SESSION_RECV_BUF_SIZE);
            break;
        }
        else {
        	memset(rx_buf,0,RTSP_SESSION_RECV_BUF_SIZE);
        	snprintf(rx_buf, RTSP_SESSION_RECV_BUF_SIZE, STR_BAD_REQUEST, session->lastSeq);
            rtsp_send(session->sess_sock, rx_buf, strlen(rx_buf), 0);
        }

        memset(rx_buf,0,sizeof(rx_buf));
    }

    // Clean up session (stop media, close sockets)
    safe_close_socket(&session->sess_sock);
    session->video.is_active = false;
    session->audio.is_active = false;
    session->audio_back.is_active = false;
    session->state = RTSP_UNINIT;
    RTSP_PRINT("rtsp session task end!\r\n");
    vTaskDelete(NULL);

}

// Main RTSP server task: accepts incoming TCP connections and creates session tasks
void rtsp_server_task(void *param)
{

	Rtsp_context_t *ctx = (Rtsp_context_t*)param;

    ctx->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listen_fd < 0) {
    	RTSP_PRINT("socket() failed\n");
    	goto end;
    }

    int opt = 1;
    setsockopt(ctx->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(RTSP_SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ctx->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    	RTSP_PRINT("bind() failed\n");
    	goto end;
    }

    if (listen(ctx->listen_fd, RTSP_MAX_SESSION_NUM) < 0) {
    	RTSP_PRINT("listen() failed\n");
    	goto end;
    }

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int client_fd = accept(ctx->listen_fd, (struct sockaddr *)&client, &len);
        if (client_fd < 0) {
        	RTSP_PRINT("accept() failed\n");
            continue;
        }

        //check for available session
        int ii;
        for (ii =0;ii<sizeof(ctx->sessions)/sizeof(ctx->sessions[0]);ii++)
        {
        	if (ctx->sessions[ii].sess_sock == -1 && ctx->sessions[ii].state == RTSP_UNINIT)
        	{
        		ctx->sessions[ii].sess_sock = client_fd;
        		ctx->sessions[ii].state = RTSP_INIT;
        		xTaskCreate(rtsp_session_task, "rtspS", RTSP_TASKS_STACK_SIZE, (void*)&ctx->sessions[ii],
        				RTSP_TASK_PRIORITY, &ctx->sessions[ii].handler);
        		RTSP_PRINT("rtspS task created!\n");
        		break;
        	}
        }

        if (ii == sizeof(ctx->sessions)/sizeof(ctx->sessions[0]))
        {
        	//no session available
        	close(client_fd);
        	RTSP_PRINT("Max sessions reached!\n");
        }
    }

end:
	close(ctx->listen_fd);
	ctx->listen_fd = -1;
	ctx->serverH = NULL;
	return;
}



// Send PDU frame as multiple RTP packets with fragmentation and JPEG header (RFC 2435)
static void send_rtp_packet(Media_stream_t *stream,
							Rtp_header_t* rtp_header,
							Jpeg_header_t* jpeg_header)
{

    uint32_t pdu_len = stream->rx_pdu->len;
    uint8_t* pdu_data = stream->rx_pdu->data;
    rtp_header->timestamp = htonl(stream->timestamp);

    if (stream->media_type == MEDIA_TYPE_VIDEO)
    {
    	uint32_t offset = 0;
    	uint32_t payload_size = 0;

    	while (offset < pdu_len)
    	{
			payload_size = pdu_len - offset;

			//update last fragment marker
			if (payload_size <= RTP_JPEG_FRAGMENT_SIZE)
			{
				rtp_header->mpt |= 0x80;
			}else
			{
				rtp_header->mpt &= ~0x80;
				payload_size = RTP_JPEG_FRAGMENT_SIZE;
			}

			//update the sequence for each fragment
			rtp_header->seq = htons(stream->seq);
			memcpy(stream->rtp_buffer,rtp_header,sizeof(Rtp_header_t));

			//update offset in  default jpeg header
			jpeg_header->fragment_offset1 = (offset >> 16) & 0xFF;
			jpeg_header->fragment_offset2 = (offset >> 8) & 0xFF;
			jpeg_header->fragment_offset3 = offset & 0xFF;
			memcpy(stream->rtp_buffer + sizeof(Rtp_header_t),jpeg_header,sizeof(Jpeg_header_t));

			//copy the payload
			memcpy(stream->rtp_buffer + sizeof(Rtp_header_t) + sizeof(Jpeg_header_t),
				   pdu_data + offset,
				   payload_size);

			sendto(stream->rtp_sock,
				   stream->rtp_buffer,
				   sizeof(Rtp_header_t) + sizeof(Jpeg_header_t) + payload_size,
				   0,
				   (struct sockaddr *)&stream->client_addr,
				   sizeof(stream->client_addr));


			stream->seq++;
			offset += payload_size;

    	}

    }else
    {
    	//don't need fragment for audio data

        //update the sequence for each fragment
        rtp_header->seq = htons(stream->seq);
		memcpy(stream->rtp_buffer,rtp_header,sizeof(Rtp_header_t));
		//copy the payload
		memcpy(stream->rtp_buffer + sizeof(Rtp_header_t) ,pdu_data, pdu_len);
		sendto(stream->rtp_sock,
        	   stream->rtp_buffer,
			   sizeof(Rtp_header_t) + pdu_len,
        	   0,
		       (struct sockaddr *)&stream->client_addr,
			   sizeof(stream->client_addr));

		stream->seq++;
    }


}



void RTSP_Init(void)
{
	if (g_rtsp_ctx.serverH != NULL)
	{
		RTSP_PRINT("Rtsp server is running. Stop it before start it again!");
	}else
	{
        // Initialize the global jitter buffer for audio backchannel
		jitter_buffer_init(&g_audio_back_jitter_buffer);

		for (int i = 0;i<sizeof(g_rtsp_ctx.sessions)/sizeof(g_rtsp_ctx.sessions[0]);i++)
		{
			g_rtsp_ctx.sessions[i].state = RTSP_UNINIT;
			g_rtsp_ctx.sessions[i].sess_sock = -1;

		}
		g_rtsp_ctx.listen_fd = -1;
		g_rtsp_ctx.serverH = NULL;


		assert(pdPASS == xTaskCreate(rtsp_server_task, "rtspServer",
				RTSP_TASKS_STACK_SIZE/2,
				&g_rtsp_ctx,
				RTSP_TASK_PRIORITY,
				&g_rtsp_ctx.serverH));
		RTSP_PRINT("Rtsp server is starting.\r\n");
	}
}
void RTSP_DeInit(void)
{
	//stop rtsp server, rtsp server task, session task, rtp task
	for (int i = 0;i<sizeof(g_rtsp_ctx.sessions)/sizeof(g_rtsp_ctx.sessions[0]);i++)
	{
		Rtsp_session_t* sess = &g_rtsp_ctx.sessions[i];
		if (RTSP_UNINIT != sess->state)
		{
			handle_teardown(sess,sess->response,RTSP_SESSION_RECV_BUF_SIZE);
			safe_close_socket(&sess->sess_sock);

			vTaskDelete(sess->handler);
			sess->state = RTSP_UNINIT;

		}

	}
	if (g_rtsp_ctx.listen_fd >= 0)
	{
		safe_close_socket(&g_rtsp_ctx.listen_fd);

		if (g_rtsp_ctx.serverH != NULL)
		{
			vTaskDelete(g_rtsp_ctx.serverH);
			RTSP_PRINT("Rtsp server is deleted!.\r\n");
			g_rtsp_ctx.serverH = NULL;
		}

	}

    // Deinitialize the jitter buffer
	jitter_buffer_deinit(&g_audio_back_jitter_buffer);
}




// Add this function in the appropriate location (e.g., after the static helper functions)

/**
 * @brief Validate and extract RTP header from received packet
 *
 * @param rtp_header Pointer to RTP header structure
 * @param total_bytes Total bytes received (including RTP header)
 * @param rtp_seq Output: extracted sequence number (host byte order)
 * @param rtp_timestamp Output: extracted timestamp (host byte order)
 * @param payload_len Output: calculated payload length
 * @return true if RTP header is valid, false otherwise
 */
static bool validate_rtp_header(Rtp_header_t *rtp_header,
                                uint16_t total_bytes,
                                uint16_t *rtp_seq,
                                uint32_t *rtp_timestamp,
                                uint16_t *payload_len)
{
    if (rtp_header == NULL || rtp_seq == NULL || rtp_timestamp == NULL || payload_len == NULL) {
        return false;
    }

    if (total_bytes < RTP_HEADER_SIZE) {
        RTSP_PRINT("RTP: Packet too small (%u bytes)\r\n", total_bytes);
        return false;
    }

    // Extract RTP header fields and convert to host byte order
    *rtp_seq = ntohs(rtp_header->seq);
    *rtp_timestamp = ntohl(rtp_header->timestamp);
    uint8_t payload_type = rtp_header->mpt & 0x7F;
    *payload_len = total_bytes - RTP_HEADER_SIZE;

    // Validate payload type
    if (payload_type != RTP_AUDIO_PAYLOAD_TYPE) {
        RTSP_PRINT("RTP: Invalid payload type: %d (expected %d)\r\n",
                   payload_type, RTP_AUDIO_PAYLOAD_TYPE);
        return false;
    }

    return true;
}
