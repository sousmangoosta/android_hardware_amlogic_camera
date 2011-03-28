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
#include <sys/time.h>
#include <unistd.h>

extern "C" unsigned char* getExifBuf(char** attrlist, int attrCount, int* exifLen,int thumbnailLen,char* thumbnaildata);
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


int CalIntLen(int content)
{
	int len = 1;
	while( (content = content/10) > 0 ) len++;
	return len;
}

int extraSmallImg(unsigned char* SrcImg,int SrcW,int SrcH,unsigned char* DstImg,int DstW,int DstH)
{
	int skipW = SrcW/DstW;
	int skipH = SrcH/DstH;

	//LOGD("skipw = %d, skipH=%d",skipW,skipH);

	unsigned char* dst = DstImg;

	unsigned char* srcrow = SrcImg;
	unsigned char* srcrowidx = srcrow;

	int i = 0,j = 0;
	for(;i<DstH;i++)
	{
		//LOGD("srcrow = %d,dst = %d",srcrow-SrcImg,dst-DstImg);
		for(j = 0;j<DstW;j++)
		{
			dst[0] = srcrowidx[0];
			dst[1] = srcrowidx[1];
			dst[2] = srcrowidx[2];
			dst+=3;
			srcrowidx+=3*skipW;
		}
	//	LOGD("srcrowidx end = %d",srcrowidx-SrcImg);

		srcrow += skipH*SrcW*3;
		srcrowidx = srcrow;
	}

	return 1;
}

int V4L2Camera::GenExif(unsigned char** pExif,int* exifLen,uint8_t* framebuf)
{	
	#define MAX_EXIF_COUNT (20)
	char* exiflist[MAX_EXIF_COUNT]={0};
	int i = 0,curend = 0;

	//Make
	exiflist[i] = new char[64];
	const char* CameraMake = m_hset.GetInfo(CAMERA_EXIF_MAKE);
	sprintf(exiflist[i],"Make=%d %s",strlen(CameraMake),CameraMake);
	i++;

	//Model
	exiflist[i] = new char[64];
	const char* CameraModel = m_hset.GetInfo(CAMERA_EXIF_MODEL);
	sprintf(exiflist[i],"Model=%d %s",strlen(CameraModel),CameraModel);
	i++;

	//Image width,height
	int width,height;
	m_hset.m_hParameter.getPictureSize(&width,&height);

	exiflist[i] = new char[64];
	sprintf(exiflist[i],"ImageWidth=%d %d",CalIntLen(width),width);
	i++;

	exiflist[i] = new char[64];
	sprintf(exiflist[i],"ImageLength=%d %d",CalIntLen(height),height);
	i++;

	//focal length  RATIONAL
	float focallen = m_hset.m_hParameter.getFloat(CameraParameters::KEY_FOCAL_LENGTH);
	int focalNum = focallen*1000;
	int focalDen = 1000;
	exiflist[i] = new char[64];
	sprintf(exiflist[i],"FocalLength=%d %d/%d",CalIntLen(focalNum)+CalIntLen(focalDen)+1,focalNum,focalDen);
	i++;

	//add gps information
	//latitude info
	char* latitudestr = (char*)m_hset.m_hParameter.get(CameraParameters::KEY_GPS_LATITUDE);
	if(latitudestr!=NULL)
	{
		int offset = 0;
		float latitude = m_hset.m_hParameter.getFloat(CameraParameters::KEY_GPS_LATITUDE);
		if(latitude < 0.0)
		{
			offset = 1;
			latitude*= (float)(-1);
		}

		int latitudedegree = latitude;
		float latitudeminuts = (latitude-(float)latitudedegree)*60;
		int latitudeminuts_int = latitudeminuts;
		float latituseconds = (latitudeminuts-(float)latitudeminuts_int)*60;
		int latituseconds_int = latituseconds;
		exiflist[i] = new char[256];
		sprintf(exiflist[i],"GPSLatitude=%d %d/%d,%d/%d,%d/%d",CalIntLen(latitudedegree)+CalIntLen(latitudeminuts_int)+CalIntLen(latituseconds_int)+8,latitudedegree,1,latitudeminuts_int,1,latituseconds_int,1);
		i++;
		
		exiflist[i] = new char[64];
		if(offset == 1)
			sprintf(exiflist[i],"GPSLatitudeRef=1 S");
		else
			sprintf(exiflist[i],"GPSLatitudeRef=1 N ");
		i++;
	}

	//Longitude info
	char* longitudestr = (char*)m_hset.m_hParameter.get(CameraParameters::KEY_GPS_LONGITUDE);
	if(longitudestr!=NULL)
	{
		int offset = 0;
		float longitude = m_hset.m_hParameter.getFloat(CameraParameters::KEY_GPS_LONGITUDE);
		if(longitude < 0.0)
		{
			offset = 1;
			longitude*= (float)(-1);
		}

		int longitudedegree = longitude;
		float longitudeminuts = (longitude-(float)longitudedegree)*60;
		int longitudeminuts_int = longitudeminuts;
		float longitudeseconds = (longitudeminuts-(float)longitudeminuts_int)*60;
		int longitudeseconds_int = longitudeseconds;
		exiflist[i] = new char[256];
		sprintf(exiflist[i],"GPSLongitude=%d %d/%d,%d/%d,%d/%d",CalIntLen(longitudedegree)+CalIntLen(longitudeminuts_int)+CalIntLen(longitudeseconds_int)+8,longitudedegree,1,longitudeminuts_int,1,longitudeseconds_int,1);
		i++;

		exiflist[i] = new char[64];
		if(offset == 1)
			sprintf(exiflist[i],"GPSLongitudeRef=1 W");
		else
			sprintf(exiflist[i],"GPSLongitudeRef=1 E");
		i++;
	}

	//Altitude info
	char* altitudestr = (char*)m_hset.m_hParameter.get(CameraParameters::KEY_GPS_ALTITUDE);
	if(altitudestr!=NULL)
	{
		int offset = 0;
		float altitude = m_hset.m_hParameter.getFloat(CameraParameters::KEY_GPS_ALTITUDE);
		if(altitude < 0.0)
		{
			offset = 1;
			altitude*= (float)(-1);
		}

		int altitudenum = altitude*1000;
		int altitudedec= 1000;
		exiflist[i] = new char[256];
		sprintf(exiflist[i],"GPSAltitude=%d %d/%d",CalIntLen(altitudenum)+CalIntLen(altitudedec)+1,altitudenum,altitudedec);
		i++;

		exiflist[i] = new char[64];
		sprintf(exiflist[i],"GPSAltitudeRef=1 %d",offset);
		i++;
	}

	//date stamp & time stamp
	time_t times = m_hset.m_hParameter.getInt(CameraParameters::KEY_GPS_TIMESTAMP);
	if(times != -1)
	{
		struct tm tmstruct;
		tmstruct = *(gmtime(&times));

		//date
		exiflist[i] = new char[128];
		char timestr[30];
		strftime(timestr, 20, "%Y:%m:%d", &tmstruct);
		sprintf(exiflist[i],"GPSDateStamp=%d %s",strlen(timestr),timestr);
		i++;

		//time
		exiflist[i] = new char[128];
		sprintf(exiflist[i],"GPSTimeStamp=%d %d/%d,%d/%d,%d/%d",CalIntLen(tmstruct.tm_hour)+CalIntLen(tmstruct.tm_min)+CalIntLen(tmstruct.tm_sec)+8,tmstruct.tm_hour,1,tmstruct.tm_min,1,tmstruct.tm_sec,1);
		i++;
	}

	//processing method
	char* processmethod = (char*)m_hset.m_hParameter.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
	if(processmethod!=NULL)
	{
		char ExifAsciiPrefix[] = { 0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0 };//asicii
		
		exiflist[i] = new char[128];
		int len = sizeof(ExifAsciiPrefix)+strlen(processmethod);
		sprintf(exiflist[i],"GPSProcessingMethod=%d ",len);
		int curend = strlen(exiflist[i]);
		memcpy(exiflist[i]+curend,ExifAsciiPrefix,8);
		memcpy(exiflist[i]+curend+8,processmethod,strlen(processmethod));
		i++;
	}

	//print exif
	int j = 0;
	for(;j<MAX_EXIF_COUNT;j++)
	{
		if(exiflist[j]!=NULL)
			LOGE("EXIF %s",exiflist[j]);
	}

	//thumbnail
	int thumbnailsize = 0;
	char* thumbnaildata = NULL;
	int thumbnailwidth = m_hset.m_hParameter.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
	int thumbnailheight = m_hset.m_hParameter.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);	
	if(thumbnailwidth > 0 )
	{
	//	LOGE("creat thumbnail data");
		//create thumbnail data
		unsigned char* rgbdata = (unsigned char*)new char[thumbnailwidth*thumbnailheight*3];
		#if 0
		int tmp;
		for(tmp = 0;tmp<thumbnailwidth*thumbnailheight*3;tmp += 3)
		{
			rgbdata[tmp] = 0;
			rgbdata[tmp+1] = 0;
			rgbdata[tmp+2] = 0;
		}
		#else
		extraSmallImg(framebuf,width,height,rgbdata,thumbnailwidth,thumbnailheight);
		#endif

		//code the thumbnail to jpeg
		thumbnaildata = new char[thumbnailwidth*thumbnailheight*3];
		jpeg_enc_t enc;
		enc.width = thumbnailwidth;
		enc.height = thumbnailheight;
		enc.quality = 90;
		enc.idata = (unsigned char*)rgbdata;	
		enc.odata = (unsigned char*)thumbnaildata;
		enc.ibuff_size =  thumbnailwidth*thumbnailheight*3;
		enc.obuff_size =  thumbnailwidth*thumbnailheight*3;
		enc.data_in_app1 = 0;
		enc.app1_data_size = 0;
		thumbnailsize = encode_jpeg(&enc);

		delete rgbdata;
	//	LOGD("after add thumbnail %d,%d len %d",thumbnailwidth,thumbnailheight,thumbnailsize);
	}

	*pExif = getExifBuf(exiflist,i,exifLen,thumbnailsize,thumbnaildata);

	//release exif
	for(i=0;i<MAX_EXIF_COUNT;i++)
	{
		if(exiflist[i]!=NULL)
			delete exiflist[i];
		exiflist[i] = NULL;
	}
	//release thumbnaildata
	if(thumbnaildata)
		delete thumbnaildata;

	return 1;
}

status_t	V4L2Camera::GetJpegFrame(uint8_t* framebuf)
{
	if(m_iPicIdx!=-1)
	{
		unsigned char* exifcontent = NULL;
		jpeg_enc_t enc;
		m_hset.m_hParameter.getPictureSize(&enc.width,&enc.height);
		enc.quality= m_hset.m_hParameter.getInt(CameraParameters::KEY_JPEG_QUALITY);
		enc.idata = (unsigned char*)pV4L2Frames[m_iPicIdx];	
		enc.odata = (unsigned char*)framebuf;
		enc.ibuff_size =  pV4L2FrameSize[m_iPicIdx];
		enc.obuff_size =  pV4L2FrameSize[m_iPicIdx];
		GenExif(&(exifcontent),&(enc.app1_data_size),(unsigned char*)pV4L2Frames[m_iPicIdx]);
		enc.data_in_app1=exifcontent+2;
		encode_jpeg(&enc);
		if(exifcontent!=0)
			free(exifcontent);
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
