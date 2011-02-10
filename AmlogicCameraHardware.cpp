/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
#define LOG_NDEBUG 0
#define NDEBUG 0

#define LOG_TAG "AmlogicCameraHardware"
#include <utils/Log.h>

#include "AmlogicCameraHardware.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <FakeCamera.h>


extern "C" {	
	
#include "tvin.h"
#include "amljpeg_enc.h"
#include "ge2d.h"


int OpenCamera(void);
int Openvdin(void);
int GetFrameData(char *buf);
int SetOsdOnOff(char* buf);
int StopCamera(void);
int Stopvdin(void);
tvin_sig_fmt_t ConvertResToDriver(int preview_width,int preview_height,int preview_FrameRate);
extern struct camera_info_s camera_info;
extern int global_w,global_h;
int GetCameraOutputData(char *buf,int dst_format);
int SetParametersToDriver(void);

jpeg_enc_t enc;
int encode_jpeg(jpeg_enc_t* enc);

}

namespace android {

//====================================================
class fakecamerainter : public CameraInterface
{
public:
	int  Open()
	{
		return 1;
	}
	int  Close()
	{
		return 1;
	}
	int  StartPreview()
	{
		return 1;
	}
	int  StopPreview()
	{
		return 1;
	}
	void InitParameters(CameraParameters& pParameters)
	{
		//set the limited & the default parameter
	    pParameters.set("preview-size-values","320x240");
	    pParameters.setPreviewSize(320, 240);
	    pParameters.setPreviewFrameRate(15);
	    pParameters.setPreviewFormat("yuv422sp");

	    pParameters.set("picture-size-values", "320x240");
	    pParameters.setPictureSize(320, 240);
	    pParameters.setPictureFormat("jpeg");

		//set the default
		SetParameters(pParameters);
	}

	void SetParameters(CameraParameters& pParameters)
	{
		return ;
	}


	void GetPreviewFrame(uint8_t* framebuf)
	{
		FakeCamera fakeCamera(320,240);
		fakeCamera.getNextFrameAsYuv422(framebuf);
	}

	void GetRawFrame(uint8_t* framebuf)
	{

	}

	void GetJpegFrame(uint8_t* framebuf)
	{		
		
	}	
};

class OV5640Camera : public CameraInterface
{
public:
	int  Open()
	{
		OpenCamera();
		sleep(2); 
		OpenCamera();  	
    	Openvdin(); 
    	//SetOsdOnOff("wc0x1d26 0x44c0");
		return 1;
	}
 	int  Close()
	{
		Stopvdin();	
		StopCamera();
		//SetOsdOnOff("wc0x1d26 0x3090");		
		return 1;
	}
	int  StartPreview()
	{
		return 1;
	}
	int  StopPreview()
	{
		return 1;
	}
	void InitParameters(CameraParameters& pParameters)
	{
		//set the limited & the default parameter

	    pParameters.set("preview-size-values","1280x720");
	    pParameters.setPreviewSize(1280, 720);
	    pParameters.setPreviewFrameRate(30);
	    pParameters.setPreviewFormat("rgb565");

	    pParameters.set("picture-size-values", "1280x720");
	    pParameters.setPictureSize(1280, 720);
	    pParameters.setPictureFormat("jpeg");	        
	    
		//set the default
		SetParameters(pParameters);
	}

	void SetParameters(CameraParameters& pParameters)
	{		
		
		int preview_width, preview_height,preview_FrameRate;
			
    	pParameters.getPreviewSize(&preview_width, &preview_height);  
    	  
    	preview_FrameRate= pParameters.getPreviewFrameRate();
    	
    	tvin_sig_fmt_t resolution_index = ConvertResToDriver(preview_width,preview_height,preview_FrameRate);
    	
    	camera_info.resolution = resolution_index;//only change resolution
    	
    	SetParametersToDriver();
    	
		LOGV("SetParameters ");
	}

	void GetPreviewFrame(uint8_t* framebuf)
	{
		 //GetFrameData((char*)framebuf);
		 GetCameraOutputData((char*)framebuf,GE2D_FORMAT_S16_RGB_565);
	}

	void GetRawFrame(uint8_t* framebuf)
	{
		LOGV("GetRawFrame ");
		GetCameraOutputData((char*)framebuf,GE2D_FORMAT_S16_RGB_565);
	}

	void GetJpegFrame(uint8_t* framebuf)
	{
		LOGV("GetJpegFrame ");
		encode_jpeg(&enc);
		
	}	
};

//=========================================================
AmlogicCameraHardware::AmlogicCameraHardware()
                  : mParameters(),
                    mPreviewHeap(0),
                    mRawHeap(0),
                    mPreviewFrameSize(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mCurrentPreviewFrame(0)
{
	mCamera = new OV5640Camera();
	mCamera->Open();
    initDefaultParameters();        
}

AmlogicCameraHardware::~AmlogicCameraHardware()
{
    singleton.clear();
	mCamera->Close();
	delete mCamera;
	mCamera = NULL;
    LOGV("~AmlogicCameraHardware ");
}

void AmlogicCameraHardware::initDefaultParameters()
{	//call the camera to return the parameter
	CameraParameters pParameters;
	mCamera->InitParameters(pParameters);
	setParameters(pParameters);
}

void AmlogicCameraHardware::initHeapLocked()
{
    // Create raw heap.
    int picture_width, picture_height;
    mParameters.getPictureSize(&picture_width, &picture_height);
    mRawHeap = new MemoryHeapBase(picture_width * 2 * picture_height);

    int preview_width, preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    LOGD("initHeapLocked: preview size=%dx%d", preview_width, preview_height);

    // Note that we enforce yuv422 in setParameters().
    int how_big = preview_width * preview_height * 2;

    // If we are being reinitialized to the same size as before, no
    // work needs to be done.
    if (how_big == mPreviewFrameSize)
        return;

    mPreviewFrameSize = how_big;

    // Make a new mmap'ed heap that can be shared across processes.
    // use code below to test with pmem
    mPreviewHeap = new MemoryHeapBase(mPreviewFrameSize * kBufferCount);
    // Make an IMemory for each frame so that we can reuse them in callbacks.
    for (int i = 0; i < kBufferCount; i++) {
        mBuffers[i] = new MemoryBase(mPreviewHeap, i * mPreviewFrameSize, mPreviewFrameSize);
    }
}


sp<IMemoryHeap> AmlogicCameraHardware::getPreviewHeap() const
{
	LOGV("getPreviewHeap");
    return mPreviewHeap;
}

sp<IMemoryHeap> AmlogicCameraHardware::getRawHeap() const
{
	LOGV("getRawHeap");
    return mRawHeap;
}

void AmlogicCameraHardware::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void AmlogicCameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void AmlogicCameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool AmlogicCameraHardware::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------

int AmlogicCameraHardware::previewThread()
{
    	mLock.lock();
        // the attributes below can change under our feet...

        int previewFrameRate = mParameters.getPreviewFrameRate();

        // Find the offset within the heap of the current buffer.
        ssize_t offset = mCurrentPreviewFrame * mPreviewFrameSize;

        sp<MemoryHeapBase> heap = mPreviewHeap;

        // this assumes the internal state of fake camera doesn't change
        // (or is thread safe)

        sp<MemoryBase> buffer = mBuffers[mCurrentPreviewFrame];

    	mLock.unlock();

    // TODO: here check all the conditions that could go wrong
    if (buffer != 0) {
        // Calculate how long to wait between frames.
        int delay = (int)(1000000.0f / float(previewFrameRate));

        // This is always valid, even if the client died -- the memory
        // is still mapped in our process.
        void *base = heap->base();

        // Fill the current frame with the fake camera.
        uint8_t *frame = ((uint8_t *)base) + offset;
 
		mParameters.getPreviewSize(&global_w, &global_h);//important
    	mCamera->GetPreviewFrame(frame);

        //LOGV("previewThread: generated frame to buffer %d", mCurrentPreviewFrame);

        // Notify the client of a new frame.
        if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)
            mDataCb(CAMERA_MSG_PREVIEW_FRAME, buffer, mCallbackCookie);

        // Advance the buffer pointer.
        mCurrentPreviewFrame = (mCurrentPreviewFrame + 1) % kBufferCount;

        // Wait for it...
        usleep(delay);
    }

    return NO_ERROR;
}

status_t AmlogicCameraHardware::startPreview()
{
	LOGV("AMLOGIC CAMERA startPreview");
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }
	mCamera->StartPreview();
	//useOverlay();
    mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}

void AmlogicCameraHardware::stopPreview()
{
	mCamera->StopPreview();
	LOGV("AMLOGIC CAMERA stopPreview");
    sp<PreviewThread> previewThread;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool AmlogicCameraHardware::previewEnabled() {
    return mPreviewThread != 0;
}

status_t AmlogicCameraHardware::startRecording()
{
    return UNKNOWN_ERROR;
}

void AmlogicCameraHardware::stopRecording()
{
}

bool AmlogicCameraHardware::recordingEnabled()
{
    return false;
}

void AmlogicCameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
}

// ---------------------------------------------------------------------------

int AmlogicCameraHardware::beginAutoFocusThread(void *cookie)
{
    AmlogicCameraHardware *c = (AmlogicCameraHardware *)cookie;
    LOGV("AMLOGIC CAMERA beginAutoFocusThread");
    return c->autoFocusThread();
}

int AmlogicCameraHardware::autoFocusThread()
{
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
    LOGV("AMLOGIC CAMERA autoFocusThread");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::autoFocus()
{
    Mutex::Autolock lock(mLock);
    LOGV("AMLOGIC CAMERA autoFocus111");	
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    LOGV("AMLOGIC CAMERA autoFocus222");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::cancelAutoFocus()
{
	LOGV("AMLOGIC CAMERA cancelAutoFocus");
    return NO_ERROR;
}

/*static*/ int AmlogicCameraHardware::beginPictureThread(void *cookie)
{
    AmlogicCameraHardware *c = (AmlogicCameraHardware *)cookie;
    LOGV("AMLOGIC CAMERA beginPictureThread");
    return c->pictureThread();
}

int AmlogicCameraHardware::pictureThread()
{
    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
	LOGV("AMLOGIC CAMERA pictureThread111");
    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        //FIXME: use a canned YUV image!
        // In the meantime just make another fake camera picture.
        
        mParameters.getPictureSize(&global_w, &global_h);//important
        
        sp<MemoryBase> mem = new MemoryBase(mRawHeap, 0, global_w * 2 * global_h);

		mCamera->GetRawFrame((uint8_t*)mRawHeap->base());

        mDataCb(CAMERA_MSG_RAW_IMAGE, mem, mCallbackCookie);
        
        LOGV("AMLOGIC CAMERA pictureThread222");
    }

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        //sp<MemoryHeapBase> heap = new MemoryHeapBase(kCannedJpegSize);
        //sp<MemoryBase> mem = new MemoryBase(heap, 0, kCannedJpegSize);
        //memcpy(heap->base(), kCannedJpeg, kCannedJpegSize);       
       
        mParameters.getPictureSize(&global_w, &global_h);
        
        //LOGE(" picture_w = (%x),picture_h = (%x)",  w,h);
        sp<MemoryHeapBase> heap = new MemoryHeapBase( global_w * 3 * global_h);
        sp<MemoryBase> mem = new MemoryBase(heap, 0, global_w * 3 * global_h);        
        
        
        sp<MemoryHeapBase> heap_input = new MemoryHeapBase( global_w * 3 * global_h);
        GetCameraOutputData((char*)heap_input->base(),GE2D_FORMAT_S24_BGR);        
        
	    enc.width=global_w;
		enc.height=global_h;	
		enc.idata = (unsigned char*)heap_input->base();	
		enc.odata = (unsigned char*)heap->base();
		enc.ibuff_size =  global_w * 3 * global_h;
		enc.obuff_size =  global_w * 3 * global_h;
		enc.quality=75;	
	
		LOGV("AMLOGIC CAMERA pictureThread333");
		
  		mCamera->GetJpegFrame((uint8_t*)heap->base());
  		
        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, mem, mCallbackCookie);
    } 
    LOGV("AMLOGIC CAMERA pictureThread444");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::takePicture()
{
    stopPreview();
    LOGV("AMLOGIC CAMERA takePicture111");
    if (createThread(beginPictureThread, this) == false)
        return -1;
    LOGV("AMLOGIC CAMERA takePicture222");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::cancelPicture()
{
	LOGV("AMLOGIC CAMERA cancelPicture");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::dump(int fd, const Vector<String16>& args) const
{
/*
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);
    if (mFakeCamera != 0) {
        mFakeCamera->dump(fd);
        mParameters.dump(fd, args);
        snprintf(buffer, 255, " preview frame(%d), size (%d), running(%s)\n", mCurrentPreviewFrame, mPreviewFrameSize, mPreviewRunning?"true": "false");
        result.append(buffer);
    } else {
        result.append("No camera client yet.\n");
    }
    write(fd, result.string(), result.size());\
*/
LOGV("AMLOGIC CAMERA dump");
    return NO_ERROR;
}

status_t AmlogicCameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);
    // to verify parameters

	//surface view is yuv422
    //if (strcmp(params.getPreviewFormat(), "yuv422sp") != 0) 
    //surface view is rgb565
    if (strcmp(params.getPreviewFormat(), "rgb565") != 0)     	
    {
		LOGE(params.getPreviewFormat());
        LOGE("Only rgb565 preview is supported  ");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    int w, h;
    params.getPictureSize(&w, &h);
/*    if (w != kCannedJpegWidth && h != kCannedJpegHeight) {
        LOGE("Still picture size must be size of canned JPEG (%dx%d)",
             kCannedJpegWidth, kCannedJpegHeight);
        return -1;
    }*/

LOGV("AMLOGIC CAMERA setParameters");

    mParameters = params;
    initHeapLocked();
	mCamera->SetParameters(mParameters);//set to the real hardware
    return NO_ERROR;
}

CameraParameters AmlogicCameraHardware::getParameters() const
{
    Mutex::Autolock lock(mLock);
    LOGV("AMLOGIC CAMERA getParameters");	
    return mParameters;
}

status_t AmlogicCameraHardware::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    return BAD_VALUE;
}

void AmlogicCameraHardware::release()
{

}

wp<CameraHardwareInterface> AmlogicCameraHardware::singleton;

sp<CameraHardwareInterface> AmlogicCameraHardware::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new AmlogicCameraHardware());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
	LOGV("CameraHardwreStub::openCameraHardware");
    return AmlogicCameraHardware::createInstance();
}





}; // namespace android


