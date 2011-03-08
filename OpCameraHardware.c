#define LOG_NDEBUG 0
//#define NDEBUG 0
#define LOG_TAG "OpCameraHardware"
#include <utils/Log.h>
#include "tvin.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <cutils/properties.h>	
	
struct camera_info_s camera_info = 
{
	.camera_name = NULL,
    .saturation = SATURATION_0_STEP,
    .brighrness = BRIGHTNESS_0_STEP,
    .contrast = CONTRAST_0_STEP,
    .hue = HUE_0_DEGREE,
    .exposure = EXPOSURE_0_STEP,
    .sharpness = SHARPNESS_AUTO_STEP,
    .mirro_flip = MF_NORMAL,
    .resolution = TVIN_SIG_FMT_CAMERA_640X480P_30Hz,
    .night_mode = CAM_NM_AUTO,
    .effect = CAM_EFFECT_ENC_NORMAL,
    .white_balance = CAM_WB_AUTO,
    .qulity = 75,
};
char cameraName[PROPERTY_VALUE_MAX];

int OpenCamera(void)
{
 	int camera_fd, ret  = 0;

	/*open iotcl close*/
	camera_fd = open("/dev/camera0", O_RDWR);
	//printf("camera device open, fd is %d \n ", camera_fd);
	if(camera_fd < 0)
	{		
		ret = -1;	
		LOGV("AMLOGIC CAMERA OpenCamera camera0 fail !! ");
		//printf("camera device  open return error string: %s\n", strerror(errno));
	}
	else
	{
		if (ioctl(camera_fd, CAMERA_IOC_START, &camera_info)<0)
		{
			ret = -1;
			LOGV("AMLOGIC CAMERA open successful !! ");
		}
		close(camera_fd);
	}
	
	return ret ;
}

int StopCamera(void)
{
	int camera_fd, ret = 0;
	//struct camera_info_s camera_info;
	camera_fd = open("/dev/camera0", O_RDWR);
	if(camera_fd < 0)
	{		
		LOGV("AMLOGIC CAMERA open error !! ");
		ret = -1;
	}
	else
	{
		if (ioctl(camera_fd, CAMERA_IOC_STOP, &camera_info)<0)
		{	
			LOGV("AMLOGIC CAMERA open successful !! ");
			ret = -1;
		}
		close(camera_fd);			
	}
	return ret ;
}

int SetOsdOnOff(char* buf)
{
	int fd;	 
	//fd = open("/sys/class/graphics/fb0/blank", O_RDWR);
	//fd = open("/sys/class/amhdmitx/amhdmitx0/debug", O_RDWR);	
	fd = open("/sys/bus/pseudo/drivers/amlogic_debug/amlogic_reg", O_RDWR);
	if (fd<0)
	{
		LOGV("osd open error !! ");
		return -1;
	}
	else
	{
		LOGV("osd open successful !! ");
		lseek(fd,0,2);
		write(fd,buf,15);
		close(fd);				
	}
	return 0;		
}

tvin_sig_fmt_t ConvertResToDriver(int preview_width,int preview_height,int preview_FrameRate)
{
	tvin_sig_fmt_t index = TVIN_SIG_FMT_NULL ;

    switch (preview_FrameRate)
    {
    	case 30:
    		if ((preview_width==640)&&(preview_height==480))
    		 	index = TVIN_SIG_FMT_CAMERA_640X480P_30Hz;
    		else if ((preview_width==800)&&(preview_height==600))
    		    index = TVIN_SIG_FMT_CAMERA_800X600P_30Hz;
    		else if ((preview_width==1024)&&(preview_height==768))
    		 	index = TVIN_SIG_FMT_CAMERA_1024X768P_30Hz;
    		else if ((preview_width==1280)&&(preview_height==720))
    		    index = TVIN_SIG_FMT_CAMERA_1280X720P_30Hz;
    		else if ((preview_width==1920)&&(preview_height==1080))
    		    index = TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz;
    	break;
     	case 25:    	
    	break;   	
      	case 15:    	
    	break;  
    	default:
    		//ret = TVIN_SIG_FMT_CAMERA_1280X720P_30Hz;
    	break;
    }
    return index;
}

int SetParametersToDriver(void)
{
 	int camera_fd ,ret = 0;

	/*open iotcl close*/
	camera_fd = open("/dev/camera0", O_RDWR);
	//printf("camera device open, fd is %d \n ", camera_fd);
	if(camera_fd < 0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA open camera0 fail !! ");
		//printf("camera device  open return error string: %s\n", strerror(errno));
	}
	else
	{
		if(ioctl(camera_fd, CAMERA_IOC_SET_PARA, &camera_info)<0)
		{	
			ret = -1;
			LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
		}
		close(camera_fd);
	}	
	return ret ;
}

char * getCameraName(void)
{
	memset(cameraName,0,sizeof(cameraName));
	property_get("camera.name", cameraName, "camera");
	return cameraName;
}

tvin_sig_fmt_t getCameraResolution(int *width,int *height)
{
	char resolution[PROPERTY_VALUE_MAX];
	char *buf,*tmp;
	tvin_sig_fmt_t ret;
	buf = resolution;
	property_get("camera.resolution", resolution, "640x480");
	tmp = strsep(&buf,"x");
	*width = atoi(tmp);
	*height = atoi(buf);
	if ((*width==640)&&(*height==480))
		ret = TVIN_SIG_FMT_CAMERA_640X480P_30Hz;
	else if ((*width==800)&&(*height==600))
		ret = TVIN_SIG_FMT_CAMERA_800X600P_30Hz;
	else if ((*width==1024)&&(*height==768))
		ret = TVIN_SIG_FMT_CAMERA_1024X768P_30Hz;
	else if ((*width==1280)&&(*height==720))
		ret = TVIN_SIG_FMT_CAMERA_1280X720P_30Hz;
	else if ((*width==1920)&&(*height==1080))
		ret = TVIN_SIG_FMT_CAMERA_1920X1080P_30Hz;
	else
		ret = TVIN_SIG_FMT_CAMERA_640X480P_30Hz;
	return ret;
}

camera_mirror_flip_t  getCameraMirrorFlip(void)
{
	char mirrorflip[PROPERTY_VALUE_MAX];
	property_get("camera.mirrorflip", mirrorflip, "0");
	return (camera_mirror_flip_t)atoi(mirrorflip);
}

int start_Capture(void)
{
 	int camera_fd ,ret = 0;

	/*open iotcl close*/
	camera_fd = open("/dev/camera0", O_RDWR);
	//printf("camera device open, fd is %d \n ", camera_fd);
	if(camera_fd < 0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA open camera0 fail !! ");
		//printf("camera device  open return error string: %s\n", strerror(errno));
	}
	else
	{
		if(ioctl(camera_fd, CAMERA_IOC_START_CAPTURE_PARA, &camera_info)<0)
		{	
			ret = -1;
			LOGV("AMLOGIC CAMERA CAMERA_IOC_START_CAPTURE_PARA fail !! ");
		}
		LOGV("AMLOGIC CAMERA CAMERA_IOC_START_CAPTURE_PARA !! ");
		close(camera_fd);
	}	
	return ret ;
}

int stop_Capture(void)
{
 	int camera_fd ,ret = 0;

	/*open iotcl close*/
	camera_fd = open("/dev/camera0", O_RDWR);
	//printf("camera device open, fd is %d \n ", camera_fd);
	if(camera_fd < 0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA open camera0 fail !! ");
		//printf("camera device  open return error string: %s\n", strerror(errno));
	}
	else
	{
		if(ioctl(camera_fd, CAMERA_IOC_STOP_CAPTURE_PARA, &camera_info)<0)
		{	
			ret = -1;
			LOGV("AMLOGIC CAMERA CAMERA_IOC_STOP_CAPTURE_PARA fail !! ");
		}
		LOGV("AMLOGIC CAMERA CAMERA_IOC_STOP_CAPTURE_PARA !! ");
		close(camera_fd);
	}	
	return ret ;
}
int SetExposure(const char *sbn)
{
 	int camera_fd ,ret = 0;
	enum camera_exposure_e bn=0;

	if(strcasecmp(sbn,"4")==0)
		bn=EXPOSURE_P4_STEP;
	else if(strcasecmp(sbn,"3")==0)
		bn=EXPOSURE_P3_STEP;
	else if(strcasecmp(sbn,"2")==0)
		bn=EXPOSURE_P2_STEP;
	else if(strcasecmp(sbn,"1")==0)
		bn=EXPOSURE_P1_STEP;
	else if(strcasecmp(sbn,"0")==0)
		bn=EXPOSURE_0_STEP;
	else if(strcasecmp(sbn,"-1")==0)
		bn=EXPOSURE_N1_STEP;
	else if(strcasecmp(sbn,"-2")==0)
		bn=EXPOSURE_N2_STEP;
	else if(strcasecmp(sbn,"-3")==0)
		bn=EXPOSURE_N3_STEP;
	else if(strcasecmp(sbn,"-4")==0)
		bn=EXPOSURE_N4_STEP;

	if(camera_info.exposure!=bn){
		camera_info.exposure =bn;
		LOGV("AMLOGIC CAMERA SetExposure!! ");

		/*open iotcl close*/
		camera_fd = open("/dev/camera0", O_RDWR);
		//printf("camera device open, fd is %d \n ", camera_fd);
		if(camera_fd < 0)
			{	
			ret = -1;
			LOGV("AMLOGIC CAMERA open camera0 fail !! ");
			//printf("camera device  open return error string: %s\n", strerror(errno));
			}
		else
			{
			if(ioctl(camera_fd, CAMERA_IOC_SET_PARA, &camera_info)<0)
				{	
				ret = -1;
				LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
				}
			close(camera_fd);
			}	
		}
	return ret ;
}

int set_white_balance(const char *swb)
{
 	int camera_fd ,ret = 0;
	enum camera_wb_flip_e wb=0;

	if(strcasecmp(swb,"auto")==0)
		wb=CAM_WB_AUTO;
	else if(strcasecmp(swb,"daylight")==0)
		wb=CAM_WB_DAYLIGHT;
	else if(strcasecmp(swb,"incandescent")==0)
		wb=CAM_WB_INCANDESCENCE;
	else if(strcasecmp(swb,"fluorescent")==0)
		wb=CAM_WB_INCANDESCENCE;


	if(camera_info.white_balance!=wb){
		camera_info.white_balance =wb;

		/*open iotcl close*/
		LOGV("AMLOGIC CAMERA set_white_balance!! ");
		camera_fd = open("/dev/camera0", O_RDWR);
		//printf("camera device open, fd is %d \n ", camera_fd);
		if(camera_fd < 0)
			{	
			ret = -1;
			LOGV("AMLOGIC CAMERA open camera0 fail !! ");
			//printf("camera device  open return error string: %s\n", strerror(errno));
			}
		else
			{
			if(ioctl(camera_fd, CAMERA_IOC_SET_PARA, &camera_info)<0)
				{	
				ret = -1;
				LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
				}
			close(camera_fd);
			}	
		}
	return ret ;
}
int set_effect(const char *sef)
{
 	int camera_fd ,ret = 0;
	enum camera_effect_flip_e ef=0;

	if(strcasecmp(sef,"auto")==0)
		ef=CAM_EFFECT_ENC_NORMAL;
	else if(strcasecmp(sef,"negative")==0)
		ef=CAM_EFFECT_ENC_COLORINV;
	else if(strcasecmp(sef,"sepia")==0)
		ef=CAM_EFFECT_ENC_SEPIA;


	if(camera_info.effect!=ef){
		camera_info.effect =ef;

		/*open iotcl close*/
		LOGV("AMLOGIC CAMERA set_effect!! ");
		camera_fd = open("/dev/camera0", O_RDWR);
		//printf("camera device open, fd is %d \n ", camera_fd);
		if(camera_fd < 0)
			{	
			ret = -1;
			LOGV("AMLOGIC CAMERA open camera0 fail !! ");
			//printf("camera device  open return error string: %s\n", strerror(errno));
			}
		else
			{
			if(ioctl(camera_fd, CAMERA_IOC_SET_PARA, &camera_info)<0)
				{	
				ret = -1;
				LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
				}
			close(camera_fd);
			}	
		}
	return ret ;
}

int set_night_mode(const char *snm)
{
 	int camera_fd ,ret = 0;
	enum camera_night_mode_flip_e nm=0;

	if(strcasecmp(snm,"auto")==0)
		nm=CAM_NM_AUTO;
	else if(strcasecmp(snm,"night")==0)
		nm=CAM_NM_ENABLE;


	if(camera_info.effect!=nm){
		camera_info.effect =nm;

		/*open iotcl close*/
		LOGV("AMLOGIC CAMERA set_night_mode!! ");
		camera_fd = open("/dev/camera0", O_RDWR);
		//printf("camera device open, fd is %d \n ", camera_fd);
		if(camera_fd < 0)
			{	
			ret = -1;
			LOGV("AMLOGIC CAMERA open camera0 fail !! ");
			//printf("camera device  open return error string: %s\n", strerror(errno));
			}
		else
			{
			if(ioctl(camera_fd, CAMERA_IOC_SET_PARA, &camera_info)<0)
				{	
				ret = -1;
				LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
				}
			close(camera_fd);
			}	
		}
	return ret ;
}

int set_qulity(const char *squ)
{
 	int camera_fd ,ret = 0;

	if(strcasecmp(squ,"70")==0)
		camera_info.qulity=70;
	else if(strcasecmp(squ,"80")==0)
		camera_info.qulity=80;
	else if(strcasecmp(squ,"90")==0)
		camera_info.qulity=90;
	else
		camera_info.qulity=90;

	return ret ;
}


