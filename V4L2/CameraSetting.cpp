
#include <camera/CameraHardwareInterface.h>
#include "CameraSetting.h"

namespace android {
status_t	CameraSetting::InitParameters(CameraParameters& pParameters)
{
		LOGE("status_t	InitParameters(CameraParameters& pParameter)");
	//set the limited & the default parameter
//==========================must set parameter for CTS will check them
	pParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,CameraParameters::PIXEL_FORMAT_YUV420SP);
	pParameters.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);

	pParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
	pParameters.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);

	pParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,"15,20");
	pParameters.setPreviewFrameRate(15);

	pParameters.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,"640x480");
	pParameters.setPreviewSize(640, 480);

	pParameters.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, "800x600");
	pParameters.setPictureSize(800,600);

	//must have >2 sizes and contain "0x0"
	pParameters.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, "180x160,0x0");
	pParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, 180);
	pParameters.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT, 160);

	pParameters.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,CameraParameters::FOCUS_MODE_AUTO);		
	pParameters.set(CameraParameters::KEY_FOCUS_MODE,CameraParameters::FOCUS_MODE_AUTO);

	pParameters.set(CameraParameters::KEY_FOCAL_LENGTH,"4.31");

	pParameters.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,"54.8");
	pParameters.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,"42.5");

//==========================

	pParameters.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE,"auto,daylight,incandescent,fluorescent");
	pParameters.set(CameraParameters::KEY_WHITE_BALANCE,"auto");
	
	pParameters.set(CameraParameters::KEY_SUPPORTED_EFFECTS,"none,negative,sepia");        
	pParameters.set(CameraParameters::KEY_EFFECT,"none");

	//pParameters.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"auto,on,off,torch");		
	//pParameters.set(CameraParameters::KEY_FLASH_MODE,"auto");

	//pParameters.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,"auto,night,snow");		
	//pParameters.set(CameraParameters::KEY_SCENE_MODE,"auto");

	pParameters.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION,4);		
	pParameters.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION,-4);
	pParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP,1);		
	pParameters.set(CameraParameters::KEY_EXPOSURE_COMPENSATION,0);

#if 1
	pParameters.set(CameraParameters::KEY_ZOOM_SUPPORTED,CameraParameters::TRUE);
	pParameters.set(CameraParameters::KEY_SMOOTH_ZOOM_SUPPORTED,1);
	
	pParameters.set(CameraParameters::KEY_ZOOM_RATIOS,"100,120,140,160,180,200,220,280,300");
	pParameters.set(CameraParameters::KEY_MAX_ZOOM,8);	//think the zoom ratios as a array, the max zoom is the max index	
	pParameters.set(CameraParameters::KEY_ZOOM,0);//default should be 0
#endif

	return NO_ERROR;
}


//write parameter to v4l2 driver,
//check parameter if valid, if un-valid first should correct it ,and return the INVALID_OPERTIONA
status_t	CameraSetting::SetParameters(CameraParameters& pParameters)
{
	LOGE("status_t	InitParameters(CameraParameters& pParameters)");
	status_t rtn = NO_ERROR;
	//check zoom value
	int zoom = pParameters.getInt(CameraParameters::KEY_ZOOM);
	int maxzoom = pParameters.getInt(CameraParameters::KEY_MAX_ZOOM);
	if((zoom > maxzoom) || (zoom < 0))
	{
		rtn = INVALID_OPERATION;
		pParameters.set(CameraParameters::KEY_ZOOM, maxzoom);
	}

	m_hParameter = pParameters;

	return rtn;
}


const char* CameraSetting::GetInfo(int InfoId)
{
	switch(InfoId)
	{
		case CAMERA_EXIF_MAKE:
			return "Amlogic";
		case CAMERA_EXIF_MODEL:
			return "DEFUALT";
		default:
			return NULL;
	}
}


}
