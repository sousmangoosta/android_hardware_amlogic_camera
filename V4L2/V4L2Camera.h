#include "../AmlogicCameraHardware.h"
#include <camera/CameraHardwareInterface.h>


namespace android {

class V4L2Camera : public CameraInterface
{
public:
	V4L2Camera(char* devname);
	~V4L2Camera();
	status_t	Open() ;
	status_t	Close();
	status_t	StartPreview();
	status_t	StopPreview();
	status_t	StartRecord()	{return NO_ERROR;}
	status_t	StopRecord() {return NO_ERROR;}
	status_t	TakePicture();
	status_t	TakePictureEnd();
	status_t	StartFocus() {return NO_ERROR;}
	status_t	StopFocus() {return NO_ERROR;}
	status_t	InitParameters(CameraParameters& pParameters);
	status_t	SetParameters(CameraParameters& pParameters) ;
	status_t	GetPreviewFrame(uint8_t* framebuf) ;
	status_t	GetRawFrame(uint8_t* framebuf) ;
	status_t	GetJpegFrame(uint8_t* framebuf) ;	
	int			GetCamId() {return 0;}

protected:
	CameraParameters m_hParameter;


protected:
	//internal used for controling V4L2
	status_t    V4L2_BufferInit(int Buf_W,int Buf_H,int Buf_Num,int colorfmt);
	status_t 	V4L2_BufferUnInit();
	status_t	V4L2_BufferEnQue(int idx);
	int  		V4L2_BufferDeQue();//return the buffer index


	status_t	V4L2_StreamOn();
	status_t	V4L2_StreamOff();

	int			GenExif(unsigned char** pExif,int* exifLen);


	void**		pV4L2Frames;
	int*		pV4L2FrameSize;

	char* 		m_pDevName;
	int			m_iDevFd;
	int 		m_V4L2BufNum;
	int			m_iPicIdx;
	int			m_v4l2_qulity;
};


};
