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
#include <cutils/properties.h>
#include <camera/CameraHardwareInterface.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <linux/fb.h>


//for test
void convert_rgb16_to_yuv420sp(uint8_t *rgb, uint8_t *yuv, int width, int height);
void convert_rgb24_to_rgb16(uint8_t *rgb888, uint8_t *rgb565, int width, int height);


namespace android 
{

//for OverLay
//============================================
#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
#ifndef FBIOPUT_OSD_SRCCOLORKEY
#define  FBIOPUT_OSD_SRCCOLORKEY    0x46fb
#endif
#ifndef FBIOPUT_OSD_SRCKEY_ENABLE
#define  FBIOPUT_OSD_SRCKEY_ENABLE  0x46fa
#endif
#ifndef FBIOPUT_OSD_SET_GBL_ALPHA
#define  FBIOPUT_OSD_SET_GBL_ALPHA	0x4500
#endif

int SYS_enable_colorkey(short key_rgb565)
{
	int ret = -1;    
	int fd_fb0 = open("/dev/graphics/fb0", O_RDWR);    
	if (fd_fb0 >= 0) 
	{       
		uint32_t myKeyColor = key_rgb565;        
		uint32_t myKeyColor_en = 1;       
		printf("enablecolorkey color=%#x\n", myKeyColor);
		ret = ioctl(fd_fb0, FBIOPUT_OSD_SRCCOLORKEY, &myKeyColor);        
		ret += ioctl(fd_fb0, FBIOPUT_OSD_SRCKEY_ENABLE, &myKeyColor_en);        
		close(fd_fb0);    
	}    
	return ret;
}

int SYS_disable_colorkey()
{
	int ret = -1;    
	int fd_fb0 = open("/dev/graphics/fb0", O_RDWR);   
	if (fd_fb0 >= 0)
	{        
		uint32_t myKeyColor_en = 0;     
		ret = ioctl(fd_fb0, FBIOPUT_OSD_SRCKEY_ENABLE, &myKeyColor_en);       
		close(fd_fb0);  
	}   
	return ret;
}

static void write_sys_int(const char *path, int val)
{
    char cmd[16];
    int fd = open(path, O_RDWR);

    if(fd >= 0) {
        sprintf(cmd, "%d", val);
        write(fd, cmd, strlen(cmd));
       	close(fd);
    }
}

static void write_sys_string(const char *path, const char *s)
{
    int fd = open(path, O_RDWR);

    if(fd >= 0) {
        write(fd, s, strlen(s));
       	close(fd);
    }
}

#define DISABLE_VIDEO   "/sys/class/video/disable_video"
#define ENABLE_AVSYNC   "/sys/class/tsync/enable"
#define ENABLE_BLACKOUT "/sys/class/video/blackout_policy"
#define TSYNC_EVENT     "/sys/class/tsync/event"
#define VIDEO_ZOOM      "/sys/class/video/zoom"

static int SYS_enable_nextvideo()
{
    write_sys_int(DISABLE_VIDEO, 2);
    return 0;
}

static int SYS_disable_avsync()
{
    write_sys_int(ENABLE_AVSYNC, 0);
    return 0;
}

static int SYS_disable_video_pause()
{
    write_sys_string(TSYNC_EVENT, "VIDEO_PAUSE:0x0");
    return 0;
}

static int SYS_set_zoom(int zoom)
{
    write_sys_int(VIDEO_ZOOM, zoom);
    return 0;
}

static int SYS_reset_zoom(void)
{
    write_sys_int(VIDEO_ZOOM, 100);
    return 0;
}
#endif

extern CameraInterface* HAL_GetCameraInterface(int Id);

AmlogicCameraHardware::AmlogicCameraHardware(int camid)
                  : mParameters(),
                    mPreviewHeap(0),
                    mRawHeap(0),
                    mPreviewFrameSize(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mCurrentPreviewFrame(0),
                    mRecordEnable(0),
                    mState(0)
{
    LOGD("Current camera is %d", camid);
    mCamera = HAL_GetCameraInterface(camid);
    initDefaultParameters();
#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
    SYS_disable_avsync();
    SYS_disable_video_pause();
    SYS_enable_nextvideo();
#else
    mRecordHeap = NULL;
#endif
    mCamera->Open();
}

AmlogicCameraHardware::~AmlogicCameraHardware()
{
    singleton.clear();
	mCamera->Close();
	delete mCamera;
	mCamera = NULL;
#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
    SYS_enable_nextvideo();
    SYS_reset_zoom();
	SYS_disable_colorkey();
#endif
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
    mRawHeap = new MemoryHeapBase(picture_width * 3 * picture_height);

    int preview_width, preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
   // LOGD("initHeapLocked: preview size=%dx%d", preview_width, preview_height);

    // Note that we enforce yuv422 in setParameters().
    int how_big = preview_width * preview_height * 3 / 2;

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
    
    mRecordBufCount = kBufferCount;

	#ifndef AMLOGIC_CAMERA_OVERLAY_SUPPORT
	mRecordHeap = new MemoryHeapBase(mPreviewFrameSize * kBufferCount);
    // Make an IMemory for each frame so that we can reuse them in callbacks.
    for (int i = 0; i < kBufferCount; i++) {
        mRecordBuffers[i] = new MemoryBase(mRecordHeap, i * mPreviewFrameSize, mPreviewFrameSize);
    }
	#endif
}


sp<IMemoryHeap> AmlogicCameraHardware::getPreviewHeap() const
{
	//LOGV("getPreviewHeap");
    return mPreviewHeap;
}

sp<IMemoryHeap> AmlogicCameraHardware::getRawHeap() const
{
	//LOGV("getRawHeap");
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
	LOGD("Enable msgtype %d",msgType);
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

#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
status_t AmlogicCameraHardware::setOverlay(const sp<Overlay> &overlay)
{
	LOGD("AMLOGIC CAMERA setOverlay");
    
    if (overlay != NULL) {
        SYS_enable_colorkey(0);
    }
    
    return NO_ERROR;
}
#endif

// ---------------------------------------------------------------------------

int AmlogicCameraHardware::previewThread()
{
	mLock.lock();
	// the attributes below can change under our feet...
	int previewFrameRate = mParameters.getPreviewFrameRate();
	// Find the offset within the heap of the current buffer.
	ssize_t offset = mCurrentPreviewFrame * mPreviewFrameSize;
	sp<MemoryHeapBase> heap = mPreviewHeap;
	sp<MemoryBase> buffer = mBuffers[mCurrentPreviewFrame];
	
#ifndef AMLOGIC_CAMERA_OVERLAY_SUPPORT
	sp<MemoryBase> recordBuffer = mRecordBuffers[mCurrentPreviewFrame];
#endif
	mLock.unlock();

	if (buffer != 0) {
		int width,height;
		mParameters.getPreviewSize(&width, &height);
		int delay = (int)(1000000.0f / float(previewFrameRate)) >> 1;

		if (mRecordBufCount <= 0) {
			LOGD("Recording buffer underflow");
			usleep(delay);
			return NO_ERROR;
		}

		//get preview frames data
		void *base = heap->base();
		uint8_t *frame = ((uint8_t *)base) + offset;

		//get preview frame
		{
			if(mCamera->GetPreviewFrame(frame) != NO_ERROR)//special case for first preview frame
				    return NO_ERROR;
		
			if(mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)
			{
				//LOGD("Return preview frame");
				mDataCb(CAMERA_MSG_PREVIEW_FRAME, buffer, mCallbackCookie);
			}
		}

		//get Record frames data
		if(mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)
		{
			//LOGD("return video frame");
		#ifndef AMLOGIC_CAMERA_OVERLAY_SUPPORT
			sp<MemoryHeapBase> reocrdheap = mRecordHeap;
			sp<MemoryBase> recordbuffer = mRecordBuffers[mCurrentPreviewFrame];
			uint8_t *recordframe = ((uint8_t *)reocrdheap->base()) + offset;
			convert_rgb16_to_yuv420sp(frame,recordframe,width,height);
			mDataCbTimestamp(systemTime(),CAMERA_MSG_VIDEO_FRAME, recordbuffer, mCallbackCookie);
		#else
			android_atomic_dec(&mRecordBufCount);
			//when use overlay, the preview format is the same as record
			mDataCbTimestamp(systemTime(),CAMERA_MSG_VIDEO_FRAME, buffer, mCallbackCookie);
		#endif
		}

		mCurrentPreviewFrame = (mCurrentPreviewFrame + 1) % kBufferCount;
	}

	return NO_ERROR;
}

status_t AmlogicCameraHardware::startPreview()
{
	LOGD("AMLOGIC CAMERA startPreview");
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }
	mCamera->StartPreview();
    mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}

void AmlogicCameraHardware::stopPreview()
{
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
	mCamera->StopPreview();

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool AmlogicCameraHardware::previewEnabled() {
    return mPreviewThread.get() != 0;
}

status_t AmlogicCameraHardware::startRecording()
{
	LOGE("AmlogicCameraHardware::startRecording()");
	mCamera->StartRecord();
	mRecordEnable = true;
    return NO_ERROR;
}

void AmlogicCameraHardware::stopRecording()
{
    mCamera->StopRecord();	
	mRecordEnable = false;
}

bool AmlogicCameraHardware::recordingEnabled()
{
    return mRecordEnable;
}

void AmlogicCameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
    android_atomic_inc(&mRecordBufCount);
//	LOGD("AmlogicCameraHardware::releaseRecordingFrame, %d", mRecordBufCount);
}

// ---------------------------------------------------------------------------

int AmlogicCameraHardware::beginAutoFocusThread(void *cookie)
{
    AmlogicCameraHardware *c = (AmlogicCameraHardware *)cookie;
	//should add wait focus end
    return c->autoFocusThread();
}

int AmlogicCameraHardware::autoFocusThread()
{
	mCamera->StartFocus();
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
	{
		LOGD("return focus end msg");
    	mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
	}
	mStateLock.lock();
	mState &= ~PROCESS_FOCUS;
	mStateLock.unlock();
    return NO_ERROR;
}

status_t AmlogicCameraHardware::autoFocus()
{
	status_t ret = NO_ERROR;
    Mutex::Autolock lock(mLock);
	if(mStateLock.tryLock() == 0)
	{
		if((mState&PROCESS_FOCUS) == 0)
		{
			if (createThread(beginAutoFocusThread, this) == false)
			{
				ret = UNKNOWN_ERROR;
			}
			else
				mState |= PROCESS_FOCUS;
		}
		mStateLock.unlock();
	}

    return ret;
}

status_t AmlogicCameraHardware::cancelAutoFocus()
{
	Mutex::Autolock lock(mLock);
	return mCamera->StopFocus();
}

/*static*/ int AmlogicCameraHardware::beginPictureThread(void *cookie)
{
    AmlogicCameraHardware *c = (AmlogicCameraHardware *)cookie;
    return c->pictureThread();
}

int AmlogicCameraHardware::pictureThread()
{
    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

	mCamera->TakePicture();
	int w, h;
	mParameters.getPictureSize(&w, &h);
	//Capture picture is RGB 24 BIT
    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
		sp<MemoryBase> mem = new MemoryBase(mRawHeap, 0, w * 3 * h);
		mCamera->GetRawFrame((uint8_t*)mRawHeap->base());
		mDataCb(CAMERA_MSG_RAW_IMAGE, mem, mCallbackCookie);
    }

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
		sp<MemoryHeapBase> jpgheap = new MemoryHeapBase( w * 3 * h);
		sp<MemoryBase> jpgmem = new MemoryBase(jpgheap, 0, w * 3 * h);        
  		mCamera->GetJpegFrame((uint8_t*)jpgheap->base());
        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, jpgmem, mCallbackCookie);
    }
	mCamera->TakePictureEnd();
    return NO_ERROR;
}

status_t AmlogicCameraHardware::takePicture()
{
    stopPreview();
    if (createThread(beginPictureThread, this) == false)
        return -1;
    return NO_ERROR;
}


status_t AmlogicCameraHardware::cancelPicture()
{
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
    return NO_ERROR;
}

status_t AmlogicCameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);
    // to verify parameter

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    mParameters = params;
    initHeapLocked();

    int zoom_level = mParameters.getInt(CameraParameters::KEY_ZOOM);
    char *p = (char *)mParameters.get(CameraParameters::KEY_ZOOM_RATIOS);

    if ((p) && (zoom_level >= 0)) {
        int z = (int)strtol(p, &p, 10);
        int i = 0;
        while (i < zoom_level) {
            if (*p != ',') break;
            z = (int)strtol(p+1, &p, 10);
            i++;
        }

        SYS_set_zoom(z);
    }

    return mCamera->SetParameters(mParameters);//set to the real hardware
}

CameraParameters AmlogicCameraHardware::getParameters() const
{
	LOGE("get AmlogicCameraHardware::getParameters()");
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t AmlogicCameraHardware::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    return BAD_VALUE;
}

void AmlogicCameraHardware::release()
{
	mCamera->Close();
#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
    SYS_enable_nextvideo();
    SYS_reset_zoom();
	SYS_disable_colorkey();
#endif
}

wp<CameraHardwareInterface> AmlogicCameraHardware::singleton;

sp<CameraHardwareInterface> AmlogicCameraHardware::createInstance(int CamId)
{
    sp<CameraHardwareInterface> hardware = NULL;
    if (singleton != 0)
    {
        hardware = singleton.promote();
    }
    if (hardware == NULL)
    {
        hardware = new AmlogicCameraHardware(CamId);
        singleton = hardware;
    }

    return hardware;
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    LOGV("HAL_openCameraHardware() cameraId=%d", cameraId);
    return AmlogicCameraHardware::createInstance(cameraId);
}
}; // namespace android

void convert_rgb24_to_rgb16(uint8_t *src, uint8_t *dst, int width, int height)
{
	int src_len = width*height*3;
    int i = 0;
	int j = 0;
    
    for (i = 0; i < src_len; i += 3)
    {
		dst[j] = (src[i]&0x1f) | (src[i+1]>>5);
		dst[j+1] = ((src[i+1]>>2)<<5) | (src[i+2]>>3);
        j += 2;
    }
}


void convert_rgb16_to_yuv420sp(uint8_t *rgb, uint8_t *yuv, int width, int height)
{
	int iy =0, iuv = 0;
    uint8_t* buf_y = yuv;
    uint8_t* buf_uv = buf_y + width * height;
    uint16_t* buf_rgb = (uint16_t *)rgb;
	int h,w,val_rgb,val_r,val_g,val_b;
	int y,u,v;
    for (h = 0; h < height; h++) {
        for (w = 0; w < width; w++) {
            val_rgb = buf_rgb[h * width + w];
            val_r = ((val_rgb & (0x1f << 11)) >> 11)<<3;
            val_g = ((val_rgb & (0x3f << 5)) >> 5)<<2;
            val_b = ((val_rgb & (0x1f << 0)) >> 0)<<3;
            y = 0.30078 * val_r + 0.5859 * val_g + 0.11328 * val_b;
            if (y > 255) {
                y = 255;
            } else if (y < 0) {
                y = 0;
            }
            buf_y[iy++] = y;
            if (0 == h % 2 && 0 == w % 2) {
                u = -0.11328 * val_r - 0.33984 * val_g + 0.51179 * val_b + 128;
                if (u > 255) {
                    u = 255;
                } else if (u < 0) {
                    u = 0;
                }
                buf_uv[iuv++] = u; 
                v = 0.51179 * val_r - 0.429688 * val_g - 0.08203 * val_b  + 128;
                if (v > 255) {
                    v = 255;
                } else if (v < 0) {
                    v = 0;
                }
                buf_uv[iuv++] = v; 
            }
        }
}
}



