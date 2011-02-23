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


