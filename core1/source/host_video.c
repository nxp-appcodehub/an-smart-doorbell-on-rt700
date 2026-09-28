/*
 * Copyright 2019 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "usb_host_config.h"
#include "usb_host.h"
#include "usb_host_video.h"
#include "host_video.h"

#include "hal_camera_shared.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
/*!
 * @brief usb host video command complete callback.
 *
 * This function is used as callback function for completed command.
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status         transfer result status.
 */
static void USB_HostVideoCommandCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status);

/*!
 * @brief usb host video control transfer callback.
 *
 * This function is used as callback function for control transfer .
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status         transfer result status.
 */
static void USB_HostVideoControlCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status);

/*!
 * @brief host video stream iso in transfer callback.
 *
 * This function is used as callback function when call USB_HosVideoStreamRecv .
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status    transfer result status.
 */
static void USB_HostVideoStreamDataInCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status);

/*******************************************************************************
 * Variables
 ******************************************************************************/

usb_host_video_camera_instance_t g_Videos[USB_HOST_VIDEO_INST_NUM];

#define ENABLE_FPS_LOG 1
#define USE_NON_CACHE_MEM_FOR_PICTRUE_FRAME 0


#if ENABLE_FPS_LOG
//below vars for debugging purpose
static uint32_t g_curTime[USB_HOST_VIDEO_INST_NUM] = {0};
static uint32_t g_totalTime4Frames[USB_HOST_VIDEO_INST_NUM];
static uint32_t g_totalLen4Frames[USB_HOST_VIDEO_INST_NUM];
static uint32_t g_frameNumbers[USB_HOST_VIDEO_INST_NUM];
//static uint32_t g_packetNumbers[USB_HOST_VIDEO_INST_NUM];
static uint32_t g_maxFrameLen[USB_HOST_VIDEO_INST_NUM];
#endif

/* the current video camera's probe value */
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static usb_host_video_probe_commit_controls_t s_VideoProbe;
/* probe buffer to transfer */
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static usb_host_video_probe_commit_controls_t s_Probe[3];

#define HOST_VIDEO_USB_RX_BUF_SIZE (1024)
/* usb video stream transfer buffer */
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_streamBuffers[USB_HOST_VIDEO_INST_NUM]
							   [USB_VIDEO_STREAM_BUFFER_COUNT*HOST_VIDEO_USB_RX_BUF_SIZE];


/* Buffers for picture frame RX, pingpang buffer for each of the instance.
 * Please make sure not only the start of the buffer is aligned but also
 * each buffer(ping or pang) is aligned.
 * address 64 bytes aligned, size need 1K aligned , jpeg decoder.
 */
#define PINGPANG_BUF_LENGTH_ALIGN(len,align) (((len) + (align) - 1) / (align) * (align))
#if USE_NON_CACHE_MEM_FOR_PICTRUE_FRAME
AT_NONCACHEABLE_SECTION_ALIGN(uint8_t g_static_buf4picFrame[USB_HOST_VIDEO_INST_NUM][USB_HOST_VIDEO_PIC_BUFFER_NUM]
	[PINGPANG_BUF_LENGTH_ALIGN(TARGET_CAMERA_MAX_RESOLUTION*USB_MJPEG_COMPRESSION_RATIO/100,1024)],64);
#else
USB_RAM_ADDRESS_ALIGNMENT(64) uint8_t g_static_buf4picFrame[USB_HOST_VIDEO_INST_NUM][USB_HOST_VIDEO_PIC_BUFFER_NUM]
	[PINGPANG_BUF_LENGTH_ALIGN(TARGET_CAMERA_MAX_RESOLUTION*USB_MJPEG_COMPRESSION_RATIO/100,1024)];
#endif


/* GUID, Globally Unique Identifier used to identify stream-encoding format */
const uint8_t g_YUY2FormatGUID[] = {0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
                                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
const uint8_t g_NV12FormatGUID[] = {0x4E, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00,
                                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
const uint8_t g_M420FormatGUID[] = {0x4D, 0x34, 0x32, 0x30, 0x00, 0x00, 0x10, 0x00,
                                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
const uint8_t g_I420FormatGUID[] = {0x49, 0x34, 0x32, 0x30, 0x00, 0x00, 0x10, 0x00,
                                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

/*******************************************************************************
 * Code
 ******************************************************************************/

void USB_HostVideoSendMsg2Inst( uint8_t cmd,
		app_cb_t cb, void* para, uint8_t idx)
{
    app_msg_t msg;
	uint32_t ipsr_val = __get_IPSR();
	msg.cmd = cmd;
	msg.parameter = para;
	msg.cb = cb;

	if (ipsr_val == 0)
	{
		xQueueSend( g_Videos[idx].QueueToSend, &msg, portMAX_DELAY );
	}
	else
	{

		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xQueueSendFromISR(  g_Videos[idx].QueueToSend, &msg, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}

}

static void* USB_HostVideoGetFreeVideoInstance()
{
	for (int i = 0;i<sizeof(g_Videos)/sizeof(g_Videos[0]);i++)
	{
			if (g_Videos[i].devState == kStatus_DEV_Idle)
			{
				return &g_Videos[i];
			}
	}

	return NULL;

}

static void* USB_HostVideoDeviceHandle2VideoInstance(usb_device_handle dev_handle)
{
	for (int i = 0;i<sizeof(g_Videos)/sizeof(g_Videos[0]);i++)
	{
			if (g_Videos[i].deviceHandle == dev_handle)
			{
				return &g_Videos[i];
			}
	}

	return NULL;

}

static uint32_t USB_HostVideoInstance2Index(usb_host_video_camera_instance_t* inst)
{

	for (int i = 0;i<sizeof(g_Videos)/sizeof(g_Videos[0]);i++)
	{
			if (&g_Videos[i] == inst)
			{
				return i;
			}
	}

	return USB_HOST_VIDEO_INST_NUM;
}



static void USB_HostVideoInstanceDeinitBuffer(usb_host_video_camera_instance_t* inst)
{

	for (int i = 0;i< sizeof(inst->pictureBuffer.bufs)/sizeof(inst->pictureBuffer.bufs[0]);i++)
	{
		inst->pictureBuffer.bufs[i].dataLen = 0;
		inst->pictureBuffer.bufs[i].ID = 0;
		inst->pictureBuffer.bufs[i].size = 0;
		inst->pictureBuffer.bufs[i].timeStamp = 0;

	}

	inst->pictureBuffer.curBufIdx = USB_HOST_VIDEO_INVALID_PIC_BUFFER_IDX;
}


static int USB_HostVideoInstanceInitBuffer(usb_host_video_camera_instance_t* inst)
{
	uint8_t idx = USB_HostVideoInstance2Index(inst);

	for (int i = 0;i< sizeof(inst->pictureBuffer.bufs)/sizeof(inst->pictureBuffer.bufs[0]);i++)
	{
		inst->pictureBuffer.bufs[i].buffer = (uint8_t*)g_static_buf4picFrame[idx][i];
		inst->pictureBuffer.bufs[i].size = sizeof(g_static_buf4picFrame[idx][i]);
		inst->pictureBuffer.bufs[i].dataLen = 0;

		usb_echo("picture buf[%d] size:%d  addr:%p\r\n",i,
				inst->pictureBuffer.bufs[i].size,
				inst->pictureBuffer.bufs[i].buffer);
	}


	inst->pictureBuffer.curBufIdx = 0;
	return 0;
}

/*Return the buffer which has a whole frame inside. */
static usb_host_video_camera_instance_buf_t*
	USB_HostVideoInstancePushDataToBuffer(usb_host_video_camera_instance_t* inst,
		uint8_t* data, uint32_t len, uint32_t frameID, uint32_t timeStamp, uint8_t endofFrame, uint8_t error)
{

	usb_host_video_camera_instance_buf_t *curBuf = NULL, *nextBuf = NULL;
	uint32_t nextIdx,curIdx;
	curIdx = inst->pictureBuffer.curBufIdx;
	nextIdx = (inst->pictureBuffer.curBufIdx + 1) % USB_HOST_VIDEO_PIC_BUFFER_NUM;
	curBuf = &inst->pictureBuffer.bufs[curIdx];
	nextBuf = &inst->pictureBuffer.bufs[nextIdx];

	switch(inst->picRXState)
	{
	case HOST_VIDEO_PIC_RX_STATE_NORMAL:
		if (curBuf->dataLen > 0 && curBuf->ID == frameID)
		{

			if (curBuf->dataLen + len > curBuf->size
					||(curBuf->timeStamp != 0xFFFFFFFFUL && curBuf->timeStamp != timeStamp)
					|| error != 0
					)
			{

				/*discard the frame.*/
				curBuf->dataLen = 0;
				curBuf->ID = 0;
				curBuf->timeStamp = 0;

				if (endofFrame)
				{
					/*This is the end of the frame. don't need to go to discard state */
					inst->discardPictureCount++;

				}else
				{
					/*discard all packet until the end of the frame. */
					inst->picRXState = HOST_VIDEO_PIC_RX_STATE_DIS_ONE_FRAME;

				}
			}else
			{
				// a "good" packet received.
append_data:
				/* Append the data */
				memcpy(&curBuf->buffer[curBuf->dataLen],data,len);
				curBuf->dataLen += len;

				/* if it is the end of a frame, return the buffer index. */
				if (endofFrame)
				{
					/*switch buffer*/
					inst->pictureBuffer.curBufIdx = nextIdx;

					if (nextBuf->dataLen > 0)
					{
						/* this buffer is in use */
						inst->picRXState = HOST_VIDEO_PIC_RX_STATE_NO_BUF_AVAIL;
					}
					return curBuf;
				}
			}

		}else
		{

			/* For both cases: dataLen == 0 or ID changed, we take it as
			 * the first packet of a new frame.
			 */
			curBuf->dataLen = 0;
			if (len > 0)
			{
				curBuf->ID = frameID;
				curBuf->timeStamp = timeStamp;
				goto append_data;
			}else
			{
				//First packet is 0?
				return NULL;
			}

		}

		break;
	case HOST_VIDEO_PIC_RX_STATE_DIS_ONE_FRAME:
		/* only handle end of frame.*/
		if (endofFrame)
		{
			curBuf->dataLen = 0;
			curBuf->ID = 0;
			curBuf->timeStamp = 0;
			inst->picRXState = HOST_VIDEO_PIC_RX_STATE_NORMAL;
			inst->discardPictureCount++;
		}
		break;
	case HOST_VIDEO_PIC_RX_STATE_NO_BUF_AVAIL:
		/* Try to find an valid buffer only at the end of a frame. */
		if (endofFrame)
		{
			inst->discardPictureCount++;
			if (curBuf->dataLen == 0)
			{
				/*A free buffer is available. */
				inst->picRXState = HOST_VIDEO_PIC_RX_STATE_NORMAL;
			}

		}

		break;
	default:
		while(1);
	}

	return NULL;
}


static void USB_HostVideoInstanceCleanDataInBuffer(usb_host_video_camera_instance_buf_t* buf)
{
	if (buf != NULL)
	{
		buf->ID = 0;
		buf->dataLen = 0;
		buf->timeStamp = 0;
	}

}

/*!
 * @brief get proper endpoint maxpacksize and the subordinate interface alternate.
 *
 * This function is used select proper interface alternate setting according to the expected max packet size
 *
 * @param classHandle   the class handle.
 * @param expectMaxPacketSize   the expected max packet size
 * @param interfaceAlternate   the pointer to the selected interface alternate setting
 * @param videoIsoPacketSize  the actually used endpoint max packet size
 *
 * @retval kStatus_USB_Success        successfully get the proper interface aternate setting
 * @retval kStatus_USB_InvalidHandle  The streamInterface is NULL pointer.
 * @retval kStatus_USB_Error          There is no proper interface alternate
 */
static uint8_t USB_HostGetProperEndpointInterfaceInfo(usb_host_class_handle classHandle,
                                                      uint16_t expectMaxPacketSize,
                                                      uint8_t *interfaceAlternate,
                                                      uint16_t *videoIsoPacketSize)
{
    usb_host_video_instance_struct_t *videoInstance = (usb_host_video_instance_struct_t *)classHandle;
    usb_host_interface_t *streamInterface;
    streamInterface = (usb_host_interface_t *)videoInstance->streamIntfHandle;
    usb_host_video_descriptor_union_t descriptor;
    uint16_t epTransactionsPerMicroFrame = 0U;
    uint16_t epMaxPacketSize             = 0U;
    uint16_t length                      = 0;
    uint8_t epCount                      = 0;
    uint8_t alternate                    = 0;

    if (NULL == streamInterface)
    {
        return kStatus_USB_InvalidHandle;
    }
    if (NULL == streamInterface->interfaceDesc)
    {
        return kStatus_USB_InvalidParameter;
    }

    descriptor.bufr = streamInterface->interfaceExtension;
    length          = 0;
    while (length < streamInterface->interfaceExtensionLength)
    {
        if (descriptor.common->bDescriptorType == USB_DESCRIPTOR_TYPE_INTERFACE)
        {
            break;
        }
        length += descriptor.common->bLength;
        descriptor.bufr += descriptor.common->bLength;
    }

    while (length < streamInterface->interfaceExtensionLength)
    {
        if (descriptor.common->bDescriptorType == USB_DESCRIPTOR_TYPE_INTERFACE)
        {
            alternate = descriptor.interface->bAlternateSetting;
            epCount   = descriptor.interface->bNumEndpoints;
            while (epCount)
            {
                if (descriptor.endpoint->bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
                {
                    epTransactionsPerMicroFrame =
                        (((USB_SHORT_FROM_LITTLE_ENDIAN_DATA(descriptor.endpoint->wMaxPacketSize)) &
                          USB_DESCRIPTOR_ENDPOINT_MAXPACKETSIZE_MULT_TRANSACTIONS_MASK) >>
                         USB_DESCRIPTOR_ENDPOINT_MAXPACKETSIZE_MULT_TRANSACTIONS_SHFIT) +
                        1;
                    epMaxPacketSize = ((USB_SHORT_FROM_LITTLE_ENDIAN_DATA(descriptor.endpoint->wMaxPacketSize)) &
                                       USB_DESCRIPTOR_ENDPOINT_MAXPACKETSIZE_SIZE_MASK);
                    if (((epMaxPacketSize * epTransactionsPerMicroFrame) <= expectMaxPacketSize) &&
                        (*videoIsoPacketSize < (epMaxPacketSize * epTransactionsPerMicroFrame)))
                    {
                        *videoIsoPacketSize = epMaxPacketSize * epTransactionsPerMicroFrame;
                        /* save interface alternate for the current proper endpoint */
                        *interfaceAlternate = alternate;
                    }
                    epCount--;
                }
                length += descriptor.common->bLength;
                descriptor.bufr += descriptor.common->bLength;
            }
        }
        else
        {
            length += descriptor.common->bLength;
            descriptor.bufr += descriptor.common->bLength;
        }
    }
    if ((0 == *videoIsoPacketSize) || (0 == *interfaceAlternate))
    {
        return kStatus_USB_Error;
    }
    else
    {
        return kStatus_USB_Success;
    }
}

/*!
 * @brief usb host video command complete callback.
 *
 * This function is used as callback function for completed command.
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status         transfer result status.
 */
static void USB_HostVideoCommandCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status)
{
    usb_host_video_camera_instance_t *videoInstance = (usb_host_video_camera_instance_t *)param;
    videoInstance->isControlTransferring            = 0;
}

/*!
 * @brief usb host video control transfer callback.
 *
 * This function is used as callback function for control transfer .
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status         transfer result status.
 */
static void USB_HostVideoControlCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status)
{
    usb_host_video_camera_instance_t *videoInstance = (usb_host_video_camera_instance_t *)param;
    if (status != kStatus_USB_Success)
    {
        usb_echo("data transfer error = %d , status \r\n");
        return;
    }

    if (videoInstance->runState == kUSB_HostVideoRunIdle)
    {
        if (videoInstance->runWaitState == kUSB_HostVideoRunSetControlInterface)
        {
            videoInstance->runState = kUSB_HostVideoRunSetControlInterfaceDone;
        }
        else if (videoInstance->runWaitState == kUSB_HostVideoRunWaitSetStreamInterface)
        {
            videoInstance->runState = kUSB_HostVideoRunSetInterfaceDone;
        }
        else if (videoInstance->runWaitState == kUSB_HostVideoRunWaitGetSetProbeCommit)
        {
            videoInstance->runState = kUSB_HostVideoRunGetSetProbeCommitDone;
        }
    }
}


/*!
 * @brief host video stream iso in transfer callback.
 *
 * This function is used as callback function when call USB_HosVideoStreamRecv .
 *
 * @param param    the host video instance pointer.
 * @param data      data buffer pointer.
 * @param dataLen data length.
 * @param status    transfer result status.
 */
static void USB_HostVideoStreamDataInCallback(void *param, uint8_t *data, uint32_t dataLen, usb_status_t status)
{
    usb_host_video_camera_instance_t *videoAppInstance = (usb_host_video_camera_instance_t *)param;
    uint32_t headLength;
    int dataSize;
    uint8_t endOfFrame;
    uint8_t error;
    uint8_t ptsExist;
    uint8_t frame_id;
    uint32_t presentationTime;
    void* buf2Submit;
	static app_msg_t msg = {.cmd = USB_HOST_VIDEO_TASK_CMD_PIC_FRAME};

    uint8_t *streamBuffer =	&videoAppInstance->streamBuffer[
						videoAppInstance->streamBufferIndex*HOST_VIDEO_USB_RX_BUF_SIZE];

    if (videoAppInstance->devState != kStatus_DEV_Attached)
    {
        return;
    }
    /* the standard header is 12 bytes */
    if (dataLen < sizeof(usb_host_video_payload_header_t))
    {
    	goto submit_buffer;
    }

    endOfFrame = ((usb_host_video_payload_header_t *)streamBuffer)
                     ->HeaderInfo.bitMap.end_of_frame;
    frame_id = ((usb_host_video_payload_header_t *)streamBuffer)
                   ->HeaderInfo.bitMap.frame_id;
    error = ((usb_host_video_payload_header_t *)streamBuffer)
                           ->HeaderInfo.bitMap.error;
    ptsExist = ((usb_host_video_payload_header_t *)streamBuffer)
                                   ->HeaderInfo.bitMap.presentation_time;

    if (ptsExist)
    {
    	presentationTime = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(
        ((usb_host_video_payload_header_t *)streamBuffer)->dwPresentationTime);
    }else
    {
    	presentationTime = 0xFFFFFFFFUL;
    }

    headLength =
        ((usb_host_video_payload_header_t *)streamBuffer)->bHeaderLength;

	/*Remove the header data. */
	dataSize = dataLen - headLength;
	if (dataSize < 0)
	{
		goto submit_buffer;
	}

	/* Handle the packet even data size is 0 because some cameras
	 * may use 0 length's payload for the end of a frame.
	 */
	buf2Submit = USB_HostVideoInstancePushDataToBuffer(videoAppInstance,
														&streamBuffer[headLength],
														dataSize,
														frame_id,
														presentationTime,
														endOfFrame,
														error);

	if (buf2Submit != NULL)
	{
		/* Notify decoding task. */
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		msg.parameter = buf2Submit;
		xQueueSendFromISR( videoAppInstance->QueueToSend, &msg, &xHigherPriorityTaskWoken );
		if (xHigherPriorityTaskWoken)
		{
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}

	}

submit_buffer:
    /* prime transfer for this usb stream buffer again */
    USB_HosVideoStreamRecv(
        videoAppInstance->classHandle, streamBuffer,
		HOST_VIDEO_USB_RX_BUF_SIZE/videoAppInstance->unMultipleIsoMaxPacketSize * videoAppInstance->unMultipleIsoMaxPacketSize ,
		USB_HostVideoStreamDataInCallback,
		videoAppInstance);

    videoAppInstance->streamBufferIndex = (videoAppInstance->streamBufferIndex+1)%USB_VIDEO_STREAM_BUFFER_COUNT;

}

/*!
 * @brief write picture into sd card.
 */
static void USB_HostVideoWriteSDCard(void *param)
{

    usb_host_video_camera_instance_t *videoAppInstance = (usb_host_video_camera_instance_t *)param;
	usb_host_video_camera_instance_buf_t* buf = NULL;
	uint32_t bufLen = 0;
	app_msg_t msg;
	uint8_t idx = USB_HostVideoInstance2Index(videoAppInstance);

    OSA_SR_ALLOC();

	if( xQueueReceive(videoAppInstance->QueueToSend, &msg, portMAX_DELAY ) == pdPASS )
	{
		switch(msg.cmd)
		{
		case USB_HOST_VIDEO_TASK_CMD_PIC_FRAME:
			buf = (usb_host_video_camera_instance_buf_t*)msg.parameter;
			bufLen = buf->dataLen;
			if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG)
			{

//              uint16_t width,height;
//				width = USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(videoAppInstance->videoStreamMjpegFrameDescriptor->wWitd);
//				height = USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(videoAppInstance->videoStreamMjpegFrameDescriptor->wHeight);

				for(int ii = 0;ii<sizeof(videoAppInstance->pendingDecReq)/sizeof(videoAppInstance->pendingDecReq[0]);ii++)
				{
					usb_host_video_subscriber_t* pendMsg = &videoAppInstance->pendingDecReq[ii];
					if (pendMsg->cb)
					{
						pendMsg->cb(idx, buf->buffer, buf->dataLen, pendMsg->cbPara);
						/* clear the pending */
						pendMsg->cb = NULL;
					}

				}

				OSA_ENTER_CRITICAL();
				USB_HostVideoInstanceCleanDataInBuffer(buf);
				OSA_EXIT_CRITICAL();
			}
			else
			{
				//TODO: not implemented yet
			}

#if ENABLE_FPS_LOG
			//speed statistics
			if (g_curTime[idx] != 0)
			{

				uint32_t diff = getTime() - g_curTime[idx];
				g_curTime[idx] = getTime() ;
				g_totalTime4Frames[idx] += diff;
				g_frameNumbers[idx]++;
				g_totalLen4Frames[idx] += bufLen;
				if (g_maxFrameLen[idx] < bufLen)
				{
					g_maxFrameLen[idx] = bufLen;
				}

				/* print out FPS periodically*/
				if (g_totalTime4Frames[idx] >= 30*1000)
				{
					usb_echo("Cam[%d]:%d fps dropped:%d avgFLen:%d maxFLen:%d\r\n",idx,
							g_frameNumbers[idx]*1000/g_totalTime4Frames[idx],
							videoAppInstance->discardPictureCount,
							g_totalLen4Frames[idx]/g_frameNumbers[idx],
							g_maxFrameLen[idx]);
					g_frameNumbers[idx] = 0;
					g_totalTime4Frames[idx] = 0;
					g_totalLen4Frames[idx] = 0;


				}
			}else
			{
				/*First time clear */
				g_curTime[idx] = getTime() ;
				g_frameNumbers[idx] = 0;
				g_totalTime4Frames[idx] = 0;
				g_totalLen4Frames[idx] = 0;
			}
#endif

			break;

		case USB_HOST_VIDEO_TASK_REG_FRAME_CB_REQ:
			int ii = 0;
			for(ii = 0;ii<sizeof(videoAppInstance->pendingDecReq)/sizeof(videoAppInstance->pendingDecReq[0]);ii++)
			{
				usb_host_video_subscriber_t* pendMsg = &videoAppInstance->pendingDecReq[ii];
				if (pendMsg->cb == NULL)
				{
					pendMsg->cb = msg.cb;
					pendMsg->cbPara = msg.parameter;
					break;
				}

			}

			//no empty pending item available
			assert (ii < sizeof(videoAppInstance->pendingDecReq)/sizeof(videoAppInstance->pendingDecReq[0]));
			break;
		// case USB_HOST_VIDEO_TASK_DEREG_FRAME_CB_REQ:

		// 	for(ii = 0;ii<sizeof(videoAppInstance->pendingDecReq)/sizeof(videoAppInstance->pendingDecReq[0]);ii++)
		// 	{
		// 		usb_host_video_subscriber_t* pendMsg = &videoAppInstance->pendingDecReq[ii];
		// 		// if (pendMsg->cb == msg.cb && pendMsg->cbPara == msg.parameter)
		// 		// {
		// 		// 	pendMsg->cb = NULL;
		// 		// 	pendMsg->cbPara = NULL;
		// 		// 	break;
		// 		// }

		// 	}
		// 	break;
		default:
			assert(0);
		} //end of switch

	} // end of if( xQueueReceive(videoAppInstance->QueueToSend, &msg, portMAX_DELAY ) == pdPASS )
}

/*!
 * @brief host usb video task function.
 *
 * This function implements the host video action, it is used to create task.
 *
 * @param param  the host video instance pointer.
 */
void USB_HostVideoTask(void *param)
{
    static usb_status_t status                                   = kStatus_USB_Success;
    usb_host_video_stream_payload_frame_common_desc_t *frameDesc = NULL;
    usb_host_video_camera_instance_t *videoAppInstance           = (usb_host_video_camera_instance_t *)param;
    uint32_t index, count;
    uint32_t minResolution        = 0xFFFFFFFF;
    uint8_t minSolutionFrameIndex = 0xFF;
    uint32_t frameInterval        = 0;
    uint32_t resolution           = 0;
    uint32_t speed                = 0U;
    uint8_t i                     = 0;
	uint32_t idx = USB_HostVideoInstance2Index(videoAppInstance);
    uint32_t target_resolution = (idx == USB_HOST_VIDEO_INST_CAM_0) ? TARGET_CAMERA0_RESOLUTION : TARGET_CAMERA1_RESOLUTION;
    static TickType_t startTicks = 0;

    /* device state changes */
    if (videoAppInstance->devState != videoAppInstance->prevState)
    {
        videoAppInstance->prevState = videoAppInstance->devState;
        switch (videoAppInstance->devState)
        {
            case kStatus_DEV_Idle:
                break;

            case kStatus_DEV_Attached:
                videoAppInstance->runState                        = kUSB_HostVideoRunSetControlInterface;
                videoAppInstance->isControlTransferring           = 0;
                videoAppInstance->streamBufferIndex               = 0;
                videoAppInstance->streamBuffer = s_streamBuffers[USB_HostVideoInstance2Index(videoAppInstance)];
                videoAppInstance->discardPictureCount             = 0;
                videoAppInstance->cameraDeviceFormatType          = 0;
                videoAppInstance->unMultipleIsoMaxPacketSize      = 0;
                videoAppInstance->currentStreamInterfaceAlternate = 0;
                memset(videoAppInstance->pendingDecReq,0,sizeof(videoAppInstance->pendingDecReq));

                /* Set this to discard one frame if you would like to discard the 1st frame. */
                videoAppInstance->picRXState = HOST_VIDEO_PIC_RX_STATE_DIS_ONE_FRAME;
                videoAppInstance->step       = 0;
                USB_HostVideoInit(videoAppInstance->deviceHandle, &videoAppInstance->classHandle);
                usb_echo("USB video attached\r\n");
                break;

            case kStatus_DEV_Detached:
                videoAppInstance->devState     = kStatus_DEV_Idle;
                videoAppInstance->runState     = kUSB_HostVideoRunIdle;
                videoAppInstance->runWaitState = kStatus_DEV_Idle;
                USB_HostVideoInstanceDeinitBuffer(videoAppInstance);
                USB_HostVideoDeinit(videoAppInstance->deviceHandle, videoAppInstance->classHandle);
                videoAppInstance->classHandle = NULL;
                usb_echo("USB video detached\r\n");
                usb_echo("the total discarded picture count is %d\r\n", videoAppInstance->discardPictureCount);
                break;

            default:
                break;
        }
    }

    /* Wait 2 second for all device enumerating. if it is not, put the task into sleep. */
    if (videoAppInstance->devState == kStatus_DEV_Idle
    		&& pdTICKS_TO_MS(xTaskGetTickCount() - startTicks) > 2000)
    {
    	usb_echo("Dev:%d not found, put task in sleep.\r\n",idx);
    	vTaskDelay(portMAX_DELAY);
    }

    /* run state */
    switch (videoAppInstance->runState)
    {
        case kUSB_HostVideoRunIdle:
            break;

        case kUSB_HostVideoRunSetControlInterface:
            videoAppInstance->runWaitState = kUSB_HostVideoRunSetControlInterface;
            videoAppInstance->runState     = kUSB_HostVideoRunIdle;
            if (USB_HostVideoControlSetInterface(videoAppInstance->classHandle, videoAppInstance->controlIntfHandle, 0,
                                                 USB_HostVideoControlCallback, videoAppInstance) != kStatus_USB_Success)
            {
                usb_echo("set interface error\r\n");
            }
            break;
        case kUSB_HostVideoRunSetControlInterfaceDone:
            videoAppInstance->runWaitState                    = kUSB_HostVideoRunWaitSetStreamInterface;
            videoAppInstance->runState                        = kUSB_HostVideoRunIdle;
            videoAppInstance->currentStreamInterfaceAlternate = 0;
            if (USB_HostVideoStreamSetInterface(videoAppInstance->classHandle, videoAppInstance->streamIntfHandle,
                                                videoAppInstance->currentStreamInterfaceAlternate,
                                                USB_HostVideoControlCallback, videoAppInstance) != kStatus_USB_Success)
            {
                usb_echo("set interface error\r\n");
            }
            break;
        case kUSB_HostVideoRunSetInterfaceDone:
            videoAppInstance->runState = kUSB_HostVideoRunFindOptimalSetting;
            break;
        case kUSB_HostVideoRunFindOptimalSetting:
            /* Firstly get MJPEG format descriptor */
            status = USB_HostVideoStreamGetFormatDescriptor(videoAppInstance->classHandle,
                                                            USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG,
                                                            (void *)&videoAppInstance->videoStreamFormatDescriptor);
            if (status == kStatus_USB_InvalidHandle)
            {
                usb_echo("videoAppInstance->classHandle is invalid\r\n");
            }
            else if (status == kStatus_USB_Error)
            {
                /* the camera device doesn't support MJPEG format, try to get UNCOMPRESSED format */
                status = USB_HostVideoStreamGetFormatDescriptor(videoAppInstance->classHandle,
                                                                USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED,
                                                                (void *)&videoAppInstance->videoStreamFormatDescriptor);
                if (status == kStatus_USB_InvalidHandle)
                {
                    usb_echo("videoAppInstance->classHandle is invalid\r\n");
                }
                else if (status == kStatus_USB_Error)
                {
                    usb_echo(" host can't support this format camera device\r\n");
                    videoAppInstance->runState = kUSB_HostVideoRunIdle;
                }
                else
                {
                }
            }
            else
            {
            }

            /* Successfully get MJPEG or UNCOMPRESSED format descriptor */
            if (status == kStatus_USB_Success)
            {
                count = videoAppInstance->videoStreamFormatDescriptor->bNumFrameDescriptors;
                videoAppInstance->cameraDeviceFormatType =
                    videoAppInstance->videoStreamFormatDescriptor->bDescriptorSubtype;
                if (videoAppInstance->cameraDeviceFormatType ==
                    USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG) /* camera device supports mjpeg format */
                {
                    videoAppInstance->videoStreamMjpegFormatDescriptor =
                        (usb_host_video_stream_payload_mjpeg_format_desc_t *)
                            videoAppInstance->videoStreamFormatDescriptor;
                }
                else if (videoAppInstance->cameraDeviceFormatType ==
                         USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED) /* camera device supports uncompressed format */
                {
                    videoAppInstance->videoStreamUncompressedFormatDescriptor =
                        (usb_host_video_stream_payload_uncompressed_format_desc_t *)
                            videoAppInstance->videoStreamFormatDescriptor;
                }
                /* Choose a minimum resolution video stream frame descriptor */
                for (index = 1; index <= count; index++)
                {
                    /* Get the subordinate frame descriptor */
                    if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG)
                    {
                        status = USB_HostVideoStreamGetFrameDescriptor(
                            videoAppInstance->classHandle, videoAppInstance->videoStreamFormatDescriptor,
                            USB_HOST_DESC_SUBTYPE_VS_FRAME_MJPEG, index, (void *)&frameDesc);
                    }
                    else if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED)
                    {
                        status = USB_HostVideoStreamGetFrameDescriptor(
                            videoAppInstance->classHandle, videoAppInstance->videoStreamFormatDescriptor,
                            USB_HOST_DESC_SUBTYPE_VS_FRAME_UNCOMPRESSED, index, (void *)&frameDesc);
                    }

                    /* choose a frame descriptor that has a minimum resolution */
                    if ((kStatus_USB_Success == status) && (NULL != frameDesc))
                    {
                        resolution = ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(frameDesc->wHeight))) *
                                     ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(frameDesc->wWitd)));

                        usb_echo("resolution[%d], width:%d height:%d\r\n",index,
                        		USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(frameDesc->wWitd),
								USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(frameDesc->wHeight));

                    	if (abs(resolution - target_resolution) < minResolution)
                        {
                            minResolution         = abs(resolution - target_resolution);;
                            minSolutionFrameIndex = index;
                            if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG)
                            {
                                videoAppInstance->videoStreamMjpegFrameDescriptor =
                                    (usb_host_video_stream_payload_mjpeg_frame_desc_t *)frameDesc;
                                videoAppInstance->videoStreamUncompressedFrameDescriptor = NULL;
                            }
                            else if (videoAppInstance->cameraDeviceFormatType ==
                                     USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED)
                            {
                                videoAppInstance->videoStreamUncompressedFrameDescriptor =
                                    (usb_host_video_stream_payload_uncompressed_frame_desc_t *)frameDesc;
                                videoAppInstance->videoStreamMjpegFrameDescriptor = NULL;
                            }
                        }
                        else
                        {
                        }
                    }
                }

                /* successfully get frame descriptor that has a minimum resolution, or go into idle state */
                if (minSolutionFrameIndex != 0xFF)
                {
                    videoAppInstance->runState = kUSB_HostVideoRunGetSetProbeCommit;
                    videoAppInstance->step     = 0;
                }
                else
                {
                    status                     = kStatus_USB_Error;
                    videoAppInstance->runState = kUSB_HostVideoRunIdle;
                }
            }
            break;
        case kUSB_HostVideoRunGetSetProbeCommit:
            switch (videoAppInstance->step)
            {
                case 0:
                    /* get the current setting of device camera */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_CUR,
                                                       (void *)&s_Probe[0], USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                    /* the maximum setting for device camera */
                case 1:
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_MAX,
                                                       (void *)&s_Probe[1], USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 2:
                    /* the minimum value for device camera */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_MIN,
                                                       (void *)&s_Probe[2], USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 3:
                    /* the selected frame can support multiple interval, choose the maximum one */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG)
                        {

							/* Select a FPS close to the target. */
                        	uint32_t target_interval = 1000*1000*10/TARGET_CAMERA_FPS;
                        	uint32_t min_diff = 0xFFFFFFFFUL;
                        	uint32_t selected_interval = 0;

                        	if (videoAppInstance->videoStreamMjpegFrameDescriptor->bFrameIntervalType > 0)
                            {
                        		for (int i = 0; i < videoAppInstance->videoStreamMjpegFrameDescriptor->bFrameIntervalType; i++ )
                        		{
                                    frameInterval = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(((uint8_t*)
                                        &videoAppInstance->videoStreamMjpegFrameDescriptor->dwMinFrameInterval[i * 4]));
                                    //frame interval unit: 100ns
                                    usb_echo("FPS supported[%d]: %d\r\n", i, 1000*1000*10/frameInterval);
                                    if (min_diff > abs(frameInterval - target_interval))
                                    {
                                    	min_diff = abs(frameInterval - target_interval);
                                    	selected_interval = frameInterval;
                                    	if (min_diff == 0)
                                    	{
                                    		break;
                                    	}
                                    }

                        		}

                            }
                            else
                            {
                            	uint32_t min_frameInterval = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(
																	((uint8_t *)&(
																		 videoAppInstance->videoStreamMjpegFrameDescriptor->dwMinFrameInterval[0])));

                            	uint32_t max_frameInterval = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(
                            	                                    ((uint8_t *)&(
                            	                                         videoAppInstance->videoStreamMjpegFrameDescriptor->dwMinFrameInterval[0]) +
                            	                                     4));

                        		for (frameInterval = min_frameInterval; frameInterval <= max_frameInterval; frameInterval += 100 )
                        		{
                                    //frame interval unit: 100ns
                                    usb_echo("FPS supported[%d]: %d\r\n", i, 1000*1000*10/frameInterval);
                                    if (min_diff > abs(frameInterval - target_interval))
                                    {
                                    	min_diff = abs(frameInterval - target_interval);
                                    	selected_interval = frameInterval;
                                    	if (min_diff == 0)
                                    	{
                                    		break;
                                    	}
                                    }

                        		}
                            }

                        	usb_echo("Selected FPS: %d\r\n", 1000*1000*10/selected_interval);
                            /* save the frame interval, frame index and format index */
                            USB_LONG_TO_LITTLE_ENDIAN_DATA(selected_interval, s_VideoProbe.dwFrameInterval);
                            s_VideoProbe.bFormatIndex =
                                videoAppInstance->videoStreamMjpegFormatDescriptor->bFormatIndex;
                            s_VideoProbe.bFrameIndex = videoAppInstance->videoStreamMjpegFrameDescriptor->bFrameIndex;
                        }
                        else if (videoAppInstance->cameraDeviceFormatType ==
                                 USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED)
                        {
                            if (videoAppInstance->videoStreamUncompressedFrameDescriptor->bFrameIntervalType > 0)
                            {
                                frameInterval = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS((
                                    (uint8_t *)&(videoAppInstance->videoStreamUncompressedFrameDescriptor
                                                     ->dwMinFrameInterval[0]) +
                                    (videoAppInstance->videoStreamUncompressedFrameDescriptor->bFrameIntervalType - 1) *
                                        4));
                            }
                            else
                            {
                                frameInterval = USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(
                                    ((uint8_t *)&(videoAppInstance->videoStreamUncompressedFrameDescriptor
                                                      ->dwMinFrameInterval[0]) +
                                     4));
                            }
                            /* save the frame interval, frame index and format index */
                            USB_LONG_TO_LITTLE_ENDIAN_DATA(frameInterval, s_VideoProbe.dwFrameInterval);
                            s_VideoProbe.bFormatIndex =
                                videoAppInstance->videoStreamUncompressedFormatDescriptor->bFormatIndex;
                            s_VideoProbe.bFrameIndex =
                                videoAppInstance->videoStreamUncompressedFrameDescriptor->bFrameIndex;
                        }
                        videoAppInstance->isControlTransferring = 1;
                        /* set device camera using the new probe */
                        status = USB_HostVideoSetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_SET_CUR,
                                                       (void *)&s_VideoProbe, USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 4:
                case 5:
                case 6:
                    /* get the current/min/max state of device camera */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        if (videoAppInstance->step == 4)
                        {
                            status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_CUR,
                                                           (void *)&s_Probe[0], USB_HostVideoCommandCallback, videoAppInstance);
                        }
                        else if (videoAppInstance->step == 5)
                        {
                            status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_MAX,
                                                           (void *)&s_Probe[1], USB_HostVideoCommandCallback, videoAppInstance);
                        }
                        else
                        {
                            status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_MIN,
                                                           (void *)&s_Probe[2], USB_HostVideoCommandCallback, videoAppInstance);
                        }
                        videoAppInstance->step++;
                    }
                    break;
                case 7:
                    /* do multiple get/set cur requests to make sure a stable setting is obtained */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        memcpy((void *)&s_VideoProbe, (void *)&s_Probe[0], 22);
                        status = USB_HostVideoSetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_SET_CUR,
                                                       (void *)&s_VideoProbe, USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 8:
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_CUR,
                                                       (void *)&s_Probe[0], USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 9:
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        memcpy((void *)&s_VideoProbe, (void *)&s_Probe[0], 26);
                        status = USB_HostVideoSetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_SET_CUR,
                                                       (void *)&s_VideoProbe, USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 10:
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        status = USB_HostVideoGetProbe(videoAppInstance->classHandle, USB_HOST_VIDEO_GET_CUR,
                                                       (void *)&s_Probe[0], USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 11:
                    /* configure the hardware with the negotiated parameters */
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        videoAppInstance->isControlTransferring = 1;
                        memcpy((void *)&s_VideoProbe, (void *)&s_Probe[0], 26);
                        /* delay 20ms to make sure device is ready and then configure actually */
                        SDK_DelayAtLeastUs(20000U, SDK_DEVICE_MAXIMUM_CPU_CLOCK_FREQUENCY);
                        status = USB_HostVideoSetCommit(videoAppInstance->classHandle, USB_HOST_VIDEO_SET_CUR,
                                                        (void *)&s_VideoProbe, USB_HostVideoCommandCallback, videoAppInstance);
                        videoAppInstance->step++;
                    }
                    break;
                case 12:
                    if (videoAppInstance->isControlTransferring == 0)
                    {
                        USB_HostHelperGetPeripheralInformation(videoAppInstance->deviceHandle, kUSB_HostGetDeviceSpeed,
                                                               &speed);
                        /* According to the device speed mode, choose a proper video stream interface */
                        videoAppInstance->unMultipleIsoMaxPacketSize = 0;
                        if (USB_SPEED_FULL == speed)
                        {
                            /* if device is full speed, the selected endpoint's maxPacketSize is maximum device supports
                             * and is not more than 1023B */
                            USB_HostGetProperEndpointInterfaceInfo(videoAppInstance->classHandle,
                                                                   FULL_SPEED_ISO_MAX_PACKET_SIZE,
                                                                   &videoAppInstance->currentStreamInterfaceAlternate,
                                                                   &videoAppInstance->unMultipleIsoMaxPacketSize);
                        }
                        else if (USB_SPEED_HIGH == speed)
                        {
                            /* if device is high speed, the selected endpoint's maxPacketSize is maximum device supports
                             * and is not more than 1024B. Users can use the additional transactions (more than one
                             * transaction) per microframe by using large value instead of
                             * HIGH_SPEED_ISO_MAX_PACKET_SIZE_ZERO_ADDITION */
                            USB_HostGetProperEndpointInterfaceInfo(videoAppInstance->classHandle,
                                                                   HIGH_SPEED_ISO_MAX_PACKET_SIZE_ZERO_ADDITION,
                                                                   &videoAppInstance->currentStreamInterfaceAlternate,
                                                                   &videoAppInstance->unMultipleIsoMaxPacketSize);
                        }

                        if (videoAppInstance->currentStreamInterfaceAlternate)
                        {
                            /* set interface by the proper alternate setting */
                            videoAppInstance->runWaitState = kUSB_HostVideoRunWaitGetSetProbeCommit;
                            videoAppInstance->runState     = kUSB_HostVideoRunIdle;
                            if (USB_HostVideoStreamSetInterface(
                                    videoAppInstance->classHandle, videoAppInstance->streamIntfHandle,
                                    videoAppInstance->currentStreamInterfaceAlternate, USB_HostVideoControlCallback,
                                    videoAppInstance) != kStatus_USB_Success)
                            {
                                usb_echo("set interface error\r\n");
                            }
                        }
                        else
                        {
                            /* no proper alternate setting */
                            videoAppInstance->runWaitState = kUSB_HostVideoRunIdle;
                            videoAppInstance->runState     = kUSB_HostVideoRunIdle;
                            usb_echo("no proper alternate setting\r\n");
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        case kUSB_HostVideoRunGetSetProbeCommitDone:
        {
            /* malloc enough buffer for one raw data or mjpeg picture */
            if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_MJPEG)
            {
                /* print the camera device format info*/
                usb_echo("Camera setting is: %d(w)*%d(h)@%dfps\r\n",
                         ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                             videoAppInstance->videoStreamMjpegFrameDescriptor->wWitd))),
                         ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                             videoAppInstance->videoStreamMjpegFrameDescriptor->wHeight))),
                         10000000 / USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(s_VideoProbe.dwFrameInterval));
                /* the picture buffer byte size */
                videoAppInstance->videoCameraPictureBufferSize =
                    ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                        videoAppInstance->videoStreamMjpegFrameDescriptor->wHeight))) *
                    ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                        videoAppInstance->videoStreamMjpegFrameDescriptor->wWitd)));
                /* consider the compression rate of MJPEG, allocate a reasonable size, compression rate can be changed
                 * according to the actual situation */
                videoAppInstance->videoCameraPictureBufferSize = videoAppInstance->videoCameraPictureBufferSize / 100U;
                videoAppInstance->videoCameraPictureBufferSize =
                    videoAppInstance->videoCameraPictureBufferSize * USB_MJPEG_COMPRESSION_RATIO;
                usb_echo("picture format is MJPEG, Pic size = %d\r\n",videoAppInstance->videoCameraPictureBufferSize);
            }
            else if (videoAppInstance->cameraDeviceFormatType == USB_HOST_DESC_SUBTYPE_VS_FORMAT_UNCOMPRESSED)
            {
                usb_echo("Camera setting is: %d(w)*%d(h)@%dfps\r\n",
                         ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                             videoAppInstance->videoStreamUncompressedFrameDescriptor->wWitd))),
                         ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                             videoAppInstance->videoStreamUncompressedFrameDescriptor->wHeight))),
                         10000000 / USB_LONG_FROM_LITTLE_ENDIAN_ADDRESS(s_VideoProbe.dwFrameInterval));
                /* Uncompressed format has no compression */
                videoAppInstance->videoCameraPictureBufferSize =
                    ((uint32_t)(videoAppInstance->videoStreamUncompressedFormatDescriptor->bBitsPerPixel / 8U *
                                USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                                    videoAppInstance->videoStreamUncompressedFrameDescriptor->wHeight))) *
                    ((uint32_t)(USB_SHORT_FROM_LITTLE_ENDIAN_ADDRESS(
                        videoAppInstance->videoStreamUncompressedFrameDescriptor->wWitd)));
                /* allocate one more max packet size buffer to avoid array out of bounds access  */
                videoAppInstance->videoCameraPictureBufferSize += videoAppInstance->unMultipleIsoMaxPacketSize;
                usb_echo("picture format is ");
                if (memcmp(&videoAppInstance->videoStreamUncompressedFormatDescriptor->guidFormat[0],
                           &g_YUY2FormatGUID[0], 16) == 0)
                {
                    usb_echo("YUY2\r\n");
                }
                else if (memcmp(&videoAppInstance->videoStreamUncompressedFormatDescriptor->guidFormat[0],
                                &g_NV12FormatGUID[0], 16) == 0)
                {
                    usb_echo("NV12\r\n");
                }
                else if (memcmp(&videoAppInstance->videoStreamUncompressedFormatDescriptor->guidFormat[0],
                                &g_M420FormatGUID[0], 16) == 0)
                {
                    usb_echo("M420\r\n");
                }
                else if (memcmp(&videoAppInstance->videoStreamUncompressedFormatDescriptor->guidFormat[0],
                                &g_I420FormatGUID[0], 16) == 0)
                {
                    usb_echo("I420\r\n");
                }
                else
                {
                    /* directly print GUID*/
                    for (uint8_t index = 0; index < 16; ++index)
                    {
                        usb_echo("%x ", videoAppInstance->videoStreamUncompressedFormatDescriptor->guidFormat[index]);
                    }
                    usb_echo("\r\n");
                }
            }
            if (USB_HostVideoInstanceInitBuffer(videoAppInstance))
            {
                videoAppInstance->runState     = kUSB_HostVideoRunIdle;
                videoAppInstance->runWaitState = kUSB_HostVideoRunIdle;
                return;
            }

            /*Create queue for receiving picture buffer. */
            if (videoAppInstance->QueueToSend == NULL)
            {
            	videoAppInstance->QueueToSend = xQueueCreate( 10, sizeof(app_msg_t) );
    			assert(videoAppInstance->QueueToSend != NULL);
            }

            /* delay to make sure the device camera is ready, delay 2ms */
            vTaskDelay(pdMS_TO_TICKS(2));
            OSA_SR_ALLOC();
            OSA_ENTER_CRITICAL();
            /* prime multiple transfers */
            for (i = 0; i < USB_VIDEO_STREAM_BUFFER_COUNT; i++)
            {
                USB_HosVideoStreamRecv(videoAppInstance->classHandle,
                		&videoAppInstance->streamBuffer[i*HOST_VIDEO_USB_RX_BUF_SIZE],
						HOST_VIDEO_USB_RX_BUF_SIZE/videoAppInstance->unMultipleIsoMaxPacketSize * videoAppInstance->unMultipleIsoMaxPacketSize,
						USB_HostVideoStreamDataInCallback,
                        videoAppInstance);
            }
            OSA_EXIT_CRITICAL();
            videoAppInstance->runState = kUSB_HostVideoStart;
        	/* Notify display task, decoding complete. */
        	// vglite_task_notify(VGLITE_TASK_USB_DEV_ENUM_DONE, (void*)idx);
			// virtCamSendRsp((idx == USB_HOST_VIDEO_INST_CAM_0) ? VIRT_USB_CAM_RGBREADY : VIRT_USB_CAM_IRREADY,
			//                VIRTUAL_CAMERA_INVALID_BUFFER_ID);
            break;
        }
        case kUSB_HostVideoStart:
            USB_HostVideoWriteSDCard(videoAppInstance);

            break;
        default:
            break;
    }
}

usb_status_t USB_HostVideoEvent(usb_device_handle deviceHandle,
                                usb_host_configuration_handle configurationHandle,
                                uint32_t eventCode)
{
    usb_status_t status = kStatus_USB_Success;
    uint8_t id;
    usb_host_configuration_t *configuration;
    uint8_t interface_index;
    usb_host_interface_t *hostInterface;
    uint32_t info_value = 0U;
    usb_host_video_camera_instance_t* inst;
    static usb_device_handle g_VideoDeviceHandle = NULL;
    /*the stream interface handle , this handle is init in the class init function*/
    static usb_host_interface_handle g_VideoStreamInterfaceHandle = NULL;
    /*the control interface handle , this handle is init in the class init function*/
    static usb_host_interface_handle g_VideoControlInterfaceHandle = NULL;

    usb_echo("USB_HostVideoEvent:eventCode=0x%x, devH=%p\r\n", eventCode,deviceHandle);

    switch (eventCode)
    {
        case kUSB_HostEventAttach:
            /* judge whether is configurationHandle supported */
            configuration                 = (usb_host_configuration_t *)configurationHandle;
            g_VideoDeviceHandle           = NULL;
            g_VideoControlInterfaceHandle = NULL;
            g_VideoStreamInterfaceHandle  = NULL;
            usb_echo("configuration:if#=%d\r\n", configuration->interfaceCount);
            for (interface_index = 0U; interface_index < configuration->interfaceCount; ++interface_index)
            {
                hostInterface = &configuration->interfaceList[interface_index];
                id            = hostInterface->interfaceDesc->bInterfaceClass;
                if (id != USB_HOST_VIDEO_CLASS_CODE)
                {
                    continue;
                }
                id = hostInterface->interfaceDesc->bInterfaceSubClass;
                if (id == USB_HOST_VIDEO_SUBCLASS_CODE_CONTROL)
                {
                    g_VideoDeviceHandle           = deviceHandle;
                    g_VideoControlInterfaceHandle = hostInterface;
                    continue;
                }
                else if (id == USB_HOST_VIDEO_SUBCLASS_CODE_STREAM)
                {
                    g_VideoDeviceHandle          = deviceHandle;
                    g_VideoStreamInterfaceHandle = hostInterface;
                    continue;
                }
                else
                {
                }
            }
            if (g_VideoDeviceHandle != NULL)
            {
                return kStatus_USB_Success;
            }
            status = kStatus_USB_NotSupported;
            break;

        case kUSB_HostEventNotSupported:
            break;

        case kUSB_HostEventEnumerationDone:
        	/* Is it a existing device and video instance */
        	inst = (usb_host_video_camera_instance_t*)USB_HostVideoDeviceHandle2VideoInstance(g_VideoDeviceHandle);

        	if (inst == NULL)
        	{
        		//Allocate a new instance for this device
            	inst = (usb_host_video_camera_instance_t*)USB_HostVideoGetFreeVideoInstance();
        	}


            if (inst != NULL)
            {
                if ((g_VideoDeviceHandle != NULL) && (g_VideoControlInterfaceHandle != NULL))
                {
                	inst->devState          = kStatus_DEV_Attached;
                	inst->deviceHandle      = g_VideoDeviceHandle;
                	inst->controlIntfHandle = g_VideoControlInterfaceHandle;
                	inst->streamIntfHandle  = g_VideoStreamInterfaceHandle;

                    USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDevicePID, &info_value);
                    usb_echo("video device attached:pid=0x%x", info_value);
                    USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDeviceVID, &info_value);
                    usb_echo("vid=0x%x ", info_value);
                    USB_HostHelperGetPeripheralInformation(deviceHandle, kUSB_HostGetDeviceAddress, &info_value);
                    usb_echo("address=%d\r\n", info_value);
                }
            }
            else
            {
                usb_echo("not free device instance\r\n");
            }
            break;

        case kUSB_HostEventDetach:
        	inst = (usb_host_video_camera_instance_t*)USB_HostVideoDeviceHandle2VideoInstance(deviceHandle);
            if (inst != NULL && inst->devState != kStatus_DEV_Idle)
            {
            	inst->devState = kStatus_DEV_Detached;
            }
            break;

        default:
            break;
    }
    return status;
}
