#define LOG_NDEBUG 0
#define NDEBUG 0

#define LOG_TAG "V4L2Camera"
#include <utils/Log.h>
#include "V4L2Camera.h"
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <jpegenc/amljpeg_enc.h>
#include <cutils/properties.h>



namespace android {

#define V4L2_PREVIEW_BUFF_NUM (2)
#define V4L2_TAKEPIC_BUFF_NUM (1)


V4L2Camera::V4L2Camera(char* devname,int camid)
{
	int namelen = strlen(devname)+1;
	m_hset.m_pDevName = new char[namelen];
	strcpy(m_hset.m_pDevName,devname);
	m_hset.m_iCamId = camid;

	m_V4L2BufNum = 0;
	pV4L2Frames = NULL;
	pV4L2FrameSize = NULL;
	m_iPicIdx = -1;
}

static int opengt2005Flag=0;

status_t	V4L2Camera::Open()
{
int temp_id=-1;
char camera_b09[PROPERTY_VALUE_MAX];
	
	property_get("camera.b09", camera_b09, "camera");

	if(strcmp(camera_b09,"1")==0){
		LOGD("*****do camera_b09 special  %s\n",camera_b09);
		if(strcasecmp(m_hset.m_pDevName,"/dev/video0")==0)
    	{
    	opengt2005Flag=1;
    	}
		if((strcasecmp(m_hset.m_pDevName,"/dev/video1")==0)&&(!opengt2005Flag)&&(m_hset.m_iDevFd == -1))
		{
		  temp_id = open("/dev/video0", O_RDWR);
		  if (temp_id != -1)
		  	{
		  	LOGD("*****open %s success %d \n", "video0+++",temp_id);
			opengt2005Flag=1;
			close(temp_id);
			usleep(100);
			}
		  }
		}
	if(m_hset.m_iDevFd == -1)
	{
		m_hset.m_iDevFd = open(m_hset.m_pDevName, O_RDWR);
    	if (m_hset.m_iDevFd != -1)
		{
    		//LOGD("open %s success %d \n", m_pDevName,m_iDevFd);
      		return NO_ERROR;
    	}
		else
		{
			LOGD("open %s fail\n", m_hset.m_pDevName);
			return UNKNOWN_ERROR;
		}
	}

	return NO_ERROR;
}
status_t	V4L2Camera::Close()
{
	if(m_hset.m_iDevFd != -1)
	{
		close(m_hset.m_iDevFd);
		m_hset.m_iDevFd = -1;
	}
	return NO_ERROR;
}

status_t	V4L2Camera::InitParameters(CameraParameters& pParameters)
{
	return m_hset.InitParameters(pParameters);
}

//write parameter to v4l2 driver,
//check parameter if valid, if un-valid first should correct it ,and return the INVALID_OPERTIONA
status_t	V4L2Camera::SetParameters(CameraParameters& pParameters)
{
	return m_hset.SetParameters(pParameters);
}

status_t	V4L2Camera::StartPreview()
{
	int w,h;
	m_hset.m_hParameter.getPreviewSize(&w,&h);
	if( (NO_ERROR == V4L2_BufferInit(w,h,V4L2_PREVIEW_BUFF_NUM,V4L2_PIX_FMT_NV12))
		&& (V4L2_StreamOn() == NO_ERROR))
		return NO_ERROR;
	else
		return UNKNOWN_ERROR;
}
status_t	V4L2Camera::StopPreview()
{
	if( (NO_ERROR == V4L2_StreamOff())
		&& (V4L2_BufferUnInit() == NO_ERROR))
		return NO_ERROR;
	else
		return UNKNOWN_ERROR;
}

status_t	V4L2Camera::TakePicture()
{
	int w,h;
	m_hset.m_hParameter.getPictureSize(&w,&h);
	V4L2_BufferInit(w,h,V4L2_TAKEPIC_BUFF_NUM,V4L2_PIX_FMT_RGB24);
	V4L2_StreamOn();
	m_iPicIdx = V4L2_BufferDeQue();
	V4L2_StreamOff();
	return NO_ERROR;
}

status_t	V4L2Camera::TakePictureEnd()
{
	m_iPicIdx = -1;
	return 	V4L2_BufferUnInit();
}

status_t	V4L2Camera::GetPreviewFrame(uint8_t* framebuf)
{
	//LOGD("V4L2Camera::GetPreviewFrame\n");
	int idx = V4L2_BufferDeQue();
	memcpy((char*)framebuf,pV4L2Frames[idx],pV4L2FrameSize[idx]);
	V4L2_BufferEnQue(idx);
	return NO_ERROR;	
}

status_t	V4L2Camera::GetRawFrame(uint8_t* framebuf) 
{
	if(m_iPicIdx!=-1)
	{
		memcpy(framebuf,pV4L2Frames[m_iPicIdx],pV4L2FrameSize[m_iPicIdx]);
	}
	else
		LOGD("GetRawFraem index -1");
	return NO_ERROR;	
}
extern "C" unsigned char* getExifBuf(const char* attributes);
int V4L2Camera::GenExif(unsigned char** pExif,int* exifLen)
{
#if 0
	char* DefaultTag = "5 Make=7 AmlogicModel=6 b09refDateTime=10 2011/03/23ImageWidth=3 800ImageLength=3 600";
	unsigned char* exifinfo = getExifBuf(DefaultTag);
	*exifLen = (exifinfo[0]<<8) | (exifinfo[1]);
	LOGD("exiflen %d",exifLen);
	*pExif = exifinfo+2;
#else
	*pExif = NULL;
	*exifLen = 0;
#endif
	return 1;
}

status_t	V4L2Camera::GetJpegFrame(uint8_t* framebuf)
{
	if(m_iPicIdx!=-1)
	{
		jpeg_enc_t enc;
		m_hset.m_hParameter.getPictureSize(&enc.width,&enc.height);
		enc.quality= m_hset.m_hParameter.getInt(CameraParameters::KEY_JPEG_QUALITY);
		enc.idata = (unsigned char*)pV4L2Frames[m_iPicIdx];	
		enc.odata = (unsigned char*)framebuf;
		enc.ibuff_size =  pV4L2FrameSize[m_iPicIdx];
		enc.obuff_size =  pV4L2FrameSize[m_iPicIdx];
		GenExif(&(enc.data_in_app1),&(enc.app1_data_size));
		encode_jpeg(&enc);
	}
	else
		LOGE("GetRawFraem index -1");
	return NO_ERROR;		
}


//=======================================================================================
//functions for set V4L2
status_t V4L2Camera::V4L2_BufferInit(int Buf_W,int Buf_H,int Buf_Num,int colorfmt)
{
	struct v4l2_format hformat;
	memset(&hformat,0,sizeof(v4l2_format));
	hformat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	hformat.fmt.pix.width = Buf_W;
	hformat.fmt.pix.height = Buf_H;
	hformat.fmt.pix.pixelformat = colorfmt;
	if (ioctl(m_hset.m_iDevFd, VIDIOC_S_FMT, &hformat) == -1) 
	{
		LOGE("V4L2_BufferInit VIDIOC_S_FMT fail");
		return UNKNOWN_ERROR;
	}

	//requeset buffers in V4L2
	v4l2_requestbuffers hbuf_req;
	memset(&hbuf_req,0,sizeof(v4l2_requestbuffers));
	hbuf_req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	hbuf_req.memory = V4L2_MEMORY_MMAP;
	hbuf_req.count = Buf_Num; //just set two frames for hal have cache buffer
	if (ioctl(m_hset.m_iDevFd, VIDIOC_REQBUFS, &hbuf_req) == -1) 
	{
		LOGE("V4L2_BufferInit VIDIOC_REQBUFS fail");
		return UNKNOWN_ERROR;
	}
	else
	{
		if (hbuf_req.count < Buf_Num) 
		{
		    LOGE("V4L2_BufferInit hbuf_req.count < Buf_Num");
			return UNKNOWN_ERROR;
		}
		else//memmap these buffer to user space
		{
			pV4L2Frames = (void**)new int[Buf_Num];
			pV4L2FrameSize = new int[Buf_Num];
			int i = 0;
			v4l2_buffer hbuf_query;
			memset(&hbuf_query,0,sizeof(v4l2_buffer));

			hbuf_query.type = hbuf_req.type;
			hbuf_query.memory = V4L2_MEMORY_MMAP;
			for(;i<Buf_Num;i++)
			{
				hbuf_query.index = i;
				if (ioctl(m_hset.m_iDevFd, VIDIOC_QUERYBUF, &hbuf_query) == -1) 
				{
					LOGE("Memap V4L2 buffer Fail");
					return UNKNOWN_ERROR;
				}

				pV4L2FrameSize[i] = hbuf_query.length;
				LOGD("V4L2_BufferInit::Get Buffer Idx %d Len %d",i,pV4L2FrameSize[i]);
				pV4L2Frames[i] = mmap(NULL,pV4L2FrameSize[i],PROT_READ | PROT_WRITE,MAP_SHARED,m_hset.m_iDevFd,hbuf_query.m.offset);
				if(pV4L2Frames[i] == MAP_FAILED)
				{
					LOGE("Memap V4L2 buffer Fail");
					return UNKNOWN_ERROR;
				}
				//enqueue buffer
				if (ioctl(m_hset.m_iDevFd, VIDIOC_QBUF, &hbuf_query) == -1) 
				{
					LOGE("GetPreviewFrame nque buffer fail");
					return UNKNOWN_ERROR;
			    }
			}
			m_V4L2BufNum = Buf_Num;
		}
	}
	return NO_ERROR;
}

status_t V4L2Camera::V4L2_BufferUnInit()
{
	if(m_V4L2BufNum > 0)
	{
		//un-memmap
		int i = 0;
		for (; i < m_V4L2BufNum; i++) 
		{
			munmap(pV4L2Frames[i], pV4L2FrameSize[i]);
			pV4L2Frames[i] = NULL;
			pV4L2FrameSize[i] = 0;
		}
		m_V4L2BufNum = 0;
		delete pV4L2Frames;
		delete pV4L2FrameSize;
		pV4L2FrameSize = NULL;
		pV4L2Frames = NULL;
	}
	return NO_ERROR;
}

status_t V4L2Camera::V4L2_BufferEnQue(int idx)
{
	v4l2_buffer hbuf_query;
	memset(&hbuf_query,0,sizeof(v4l2_buffer));
	hbuf_query.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	hbuf_query.memory = V4L2_MEMORY_MMAP;//加和不加index有什么区别?
	hbuf_query.index = idx;
    if (ioctl(m_hset.m_iDevFd, VIDIOC_QBUF, &hbuf_query) == -1) 
	{
		LOGE("V4L2_BufferEnQue fail");
		return UNKNOWN_ERROR;
    }

	return NO_ERROR;
}
int  V4L2Camera::V4L2_BufferDeQue()
{
	v4l2_buffer hbuf_query;
	memset(&hbuf_query,0,sizeof(v4l2_buffer));
	hbuf_query.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	hbuf_query.memory = V4L2_MEMORY_MMAP;//加和不加index有什么区别?
    if (ioctl(m_hset.m_iDevFd, VIDIOC_DQBUF, &hbuf_query) == -1) 
	{
		LOGE("V4L2_StreamGet Deque buffer fail");
		return UNKNOWN_ERROR;
    }

	assert (hbuf_query.index < m_V4L2BufNum);
	return hbuf_query.index;	
}

status_t	V4L2Camera::V4L2_StreamOn()
{
	//LOGD("V4L2_StreamOn");
	int stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(m_hset.m_iDevFd, VIDIOC_STREAMON, &stream_type) == -1)
		LOGE("V4L2_StreamOn Fail");
	return NO_ERROR;
}

status_t	V4L2Camera::V4L2_StreamOff()
{
	int stream_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(m_hset.m_iDevFd, VIDIOC_STREAMOFF, &stream_type) == -1)
		LOGE("V4L2_StreamOff  Fail");
	return NO_ERROR;
}

//extern CameraInterface* HAL_GetFakeCamera();
extern CameraInterface* HAL_GetCameraInterface(int Id)
{
	LOGD("HAL_GetCameraInterface return V4L2 interface");
	if(Id == 0)
		return new V4L2Camera("/dev/video0",0);
	else
		return new V4L2Camera("/dev/video1",1);
}

};
