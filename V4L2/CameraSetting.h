#include <camera/CameraParameters.h>

namespace android {


#define CAMERA_EXIF_MAKE (0)
#define CAMERA_EXIF_MODEL (1)

class CameraSetting
{
	public:
		CameraSetting(){m_pDevName = NULL;m_iDevFd = -1;m_iCamId = -1;}
		~CameraSetting(){if(m_pDevName) delete m_pDevName;}
		int		m_iDevFd;
		int		m_iCamId;
		char*	m_pDevName;
		CameraParameters m_hParameter;
		
		status_t	InitParameters(CameraParameters& pParameters);
		status_t	SetParameters(CameraParameters& pParameters);
		const char* GetInfo(int InfoId);
};


}