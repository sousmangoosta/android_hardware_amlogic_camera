#define LOG_NDEBUG 0
//#define NDEBUG 0
#define LOG_TAG "OpCameraHardware"
#include <utils/Log.h>
#include "amlogic_camera_para.h"

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
#include <linux/videodev2.h>
	
int set_white_balance(int camera_fd,const char *swb)
{
 	int ret = 0;
	struct v4l2_control ctl;
	if(camera_fd<0)
		return -1;
	
	ctl.id = V4L2_CID_DO_WHITE_BALANCE;

	if(strcasecmp(swb,"auto")==0)
		ctl.value=CAM_WB_AUTO;
	else if(strcasecmp(swb,"daylight")==0)
		ctl.value=CAM_WB_DAYLIGHT;
	else if(strcasecmp(swb,"incandescent")==0)
		ctl.value=CAM_WB_INCANDESCENCE;
	else if(strcasecmp(swb,"fluorescent")==0)
		ctl.value=CAM_WB_FLUORESCENT;
		
	if(ioctl(camera_fd, VIDIOC_S_CTRL, &ctl)<0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
	}
	return ret ;
}

int SetExposure(int camera_fd,const char *sbn)
{
 	int ret = 0;
	struct v4l2_control ctl;
	if(camera_fd<0)
		return -1;
	
	ctl.id = V4L2_CID_EXPOSURE;

	if(strcasecmp(sbn,"4")==0)
		ctl.value=EXPOSURE_P4_STEP;
	else if(strcasecmp(sbn,"3")==0)
		ctl.value=EXPOSURE_P3_STEP;
	else if(strcasecmp(sbn,"2")==0)
		ctl.value=EXPOSURE_P2_STEP;
	else if(strcasecmp(sbn,"1")==0)
		ctl.value=EXPOSURE_P1_STEP;
	else if(strcasecmp(sbn,"0")==0)
		ctl.value=EXPOSURE_0_STEP;
	else if(strcasecmp(sbn,"-1")==0)
		ctl.value=EXPOSURE_N1_STEP;
	else if(strcasecmp(sbn,"-2")==0)
		ctl.value=EXPOSURE_N2_STEP;
	else if(strcasecmp(sbn,"-3")==0)
		ctl.value=EXPOSURE_N3_STEP;
	else if(strcasecmp(sbn,"-4")==0)
		ctl.value=EXPOSURE_N4_STEP;
		
	if(ioctl(camera_fd, VIDIOC_S_CTRL, &ctl)<0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
	}
	
	return ret ;
}


int set_effect(int camera_fd,const char *sef)
{
 	int ret = 0;
	struct v4l2_control ctl;
	if(camera_fd<0)
		return -1;
		
	ctl.id = V4L2_CID_COLORFX;

	if(strcasecmp(sef,"none")==0)
		ctl.value=CAM_EFFECT_ENC_NORMAL;
	else if(strcasecmp(sef,"negative")==0)
		ctl.value=CAM_EFFECT_ENC_COLORINV;
	else if(strcasecmp(sef,"sepia")==0)
		ctl.value=CAM_EFFECT_ENC_SEPIA;
		
	if(ioctl(camera_fd, VIDIOC_S_CTRL, &ctl)<0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
	}

	return ret ;
}

int set_night_mode(int camera_fd,const char *snm)
{
 	int ret = 0;
 	struct v4l2_control ctl;
	if(camera_fd<0)
		return -1;

	if(strcasecmp(snm,"auto")==0)
		ctl.value=CAM_NM_AUTO;
	else if(strcasecmp(snm,"night")==0)
		ctl.value=CAM_NM_ENABLE;
		
	ctl.id = V4L2_CID_DO_WHITE_BALANCE;

	if(ioctl(camera_fd, VIDIOC_S_CTRL, &ctl)<0)
	{	
		ret = -1;
		LOGV("AMLOGIC CAMERA SetParametersToDriver fail !! ");
	}
	return ret ;
}



