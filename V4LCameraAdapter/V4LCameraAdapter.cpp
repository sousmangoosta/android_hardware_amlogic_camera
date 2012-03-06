/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
* @file V4LCameraAdapter.cpp
*
* This file maps the Camera Hardware Interface to V4L2.
*
*/

#define LOG_NDEBUG 0
#define LOG_TAG "V4LCameraAdapter"
//reinclude because of a bug with the log macros
#include <utils/Log.h>
#include "DebugUtils.h"

#include "V4LCameraAdapter.h"
#include "CameraHal.h"
#include "TICameraParameters.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev.h>
#include <sys/time.h>

#include <cutils/properties.h>
#include <sys/types.h>
#include <sys/stat.h>


//for private_handle_t TODO move out of private header
#include <gralloc_priv.h>

#define UNLIKELY( exp ) (__builtin_expect( (exp) != 0, false ))
static int mDebugFps = 0;

#define Q16_OFFSET 16

#define HERE(Msg) {CAMHAL_LOGEB("--===line %d, %s===--\n", __LINE__, Msg);}

#define DEVICE_PATH(_sensor_index) (_sensor_index == 0 ? "/dev/video0" : "/dev/video1")

#define FLASHLIGHT_PATH "/sys/class/flashlight/flashlightctrl"

namespace android {

#undef LOG_TAG
///Maintain a separate tag for V4LCameraAdapter logs to isolate issues OMX specific
#define LOG_TAG "V4LCameraAdapter"

//redefine because of a bug with the log macros
#undef LOG_FUNCTION_NAME
#undef LOG_FUNCTION_NAME_EXIT
#define LOG_FUNCTION_NAME           LOGV("%d: %s() ENTER", __LINE__, __FUNCTION__);
#define LOG_FUNCTION_NAME_EXIT      LOGV("%d: %s() EXIT", __LINE__, __FUNCTION__);

//frames skipped before recalculating the framerate
#define FPS_PERIOD 30

Mutex gAdapterLock;

extern "C" int set_banding(int camera_fd,const char *snm);
extern "C" int set_night_mode(int camera_fd,const char *snm);
extern "C" int set_effect(int camera_fd,const char *sef);
extern "C" int SetExposure(int camera_fd,const char *sbn);
extern "C" int set_white_balance(int camera_fd,const char *swb);
extern "C" int SYS_set_zoom(int zoom);
extern "C" int get_flash_mode(void);
extern "C" int set_flash_mode(const char *sfm);
extern "C" int set_flash(bool mode);


/*--------------------junk STARTS here-----------------------------*/
#ifndef AMLOGIC_USB_CAMERA_SUPPORT
#define SYSFILE_CAMERA_SET_PARA "/sys/class/vm/attr2"
#define SYSFILE_CAMERA_SET_MIRROR "/sys/class/vm/mirror"
static int writefile(char* path,char* content)
{
    FILE* fp = fopen(path, "w+");

    LOGD("Write file %s(%p) content %s", path, fp, content);

    if (fp) {
        while( ((*content) != '\0') ) {
            if (EOF == fputc(*content,fp))
                LOGD("write char fail");
            content++;
        }

        fclose(fp);
    }
    else
        LOGD("open file fail\n");
    return 1;
}
#endif
/*--------------------Camera Adapter Class STARTS here-----------------------------*/

status_t V4LCameraAdapter::initialize(CameraProperties::Properties* caps)
{
    LOG_FUNCTION_NAME;

    char value[PROPERTY_VALUE_MAX];
    property_get("debug.camera.showfps", value, "0");
    mDebugFps = atoi(value);

    int ret = NO_ERROR;

    // Allocate memory for video info structure
    mVideoInfo = (struct VideoInfo *) calloc (1, sizeof (struct VideoInfo));
    if(!mVideoInfo)
    {
	    return NO_MEMORY;
    }

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    mUsbCameraStatus = USBCAMERA_NO_INIT;
#endif

    if ((mCameraHandle = open(DEVICE_PATH(mSensorIndex), O_RDWR)) == -1)
    {
	    CAMHAL_LOGEB("Error while opening handle to V4L2 Camera: %s", strerror(errno));
	    return -EINVAL;
    }

    ret = ioctl (mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
    if (ret < 0)
    {
	    CAMHAL_LOGEA("Error when querying the capabilities of the V4L Camera");
	    return -EINVAL;
    }

    if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
    {
	    CAMHAL_LOGEA("Error while adapter initialization: video capture not supported.");
	    return -EINVAL;
    }

    if (!(mVideoInfo->cap.capabilities & V4L2_CAP_STREAMING))
    {
	    CAMHAL_LOGEA("Error while adapter initialization: Capture device does not support streaming i/o");
	    return -EINVAL;
    }

    if (strcmp(caps->get(CameraProperties::FACING_INDEX), (const char *) android::TICameraParameters::FACING_FRONT) == 0)
        mbFrontCamera = true;
    else
        mbFrontCamera = false;
    LOGD("mbFrontCamera=%d",mbFrontCamera);

    // Initialize flags
    mPreviewing = false;
    mVideoInfo->isStreaming = false;
    mRecording = false;
    mZoomlevel = -1;

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    mUsbCameraStatus = USBCAMERA_INITED;
#endif

#ifndef AMLOGIC_USB_CAMERA_SUPPORT
    // ---------
    writefile((char*)SYSFILE_CAMERA_SET_PARA, (char*)"1");
    //mirror set at here will not work.
#endif
    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t V4LCameraAdapter::fillThisBuffer(void* frameBuf, CameraFrame::FrameType frameType)
{

    status_t ret = NO_ERROR;
    v4l2_buffer hbuf_query;
    memset(&hbuf_query,0,sizeof(v4l2_buffer));

    //LOGD("fillThisBuffer frameType=%d", frameType);
    if (CameraFrame::IMAGE_FRAME == frameType)
    {
	    //if (NULL != mEndImageCaptureCallback)
	        //mEndImageCaptureCallback(mEndCaptureData);
	    return NO_ERROR;
    }
    if ( !mVideoInfo->isStreaming || !mPreviewing)
    {
	    return NO_ERROR;
    }

    int i = mPreviewBufs.valueFor(( unsigned int )frameBuf);
    if(i<0)
    {
	    return BAD_VALUE;
    }
    if(nQueued>=mPreviewBufferCount)
    {
        CAMHAL_LOGEB("fill buffer error, reach the max preview buff:%d,max:%d",nQueued,mPreviewBufferCount);
        return BAD_VALUE;
    }

    hbuf_query.index = i;
    hbuf_query.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    hbuf_query.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &hbuf_query);
    if (ret < 0) {
       CAMHAL_LOGEB("Init: VIDIOC_QBUF %d Failed",i);
       return -1;
    }
    //CAMHAL_LOGEB("fillThis Buffer %d",i);
    nQueued++;
    return ret;

}

status_t V4LCameraAdapter::setParameters(const CameraParameters &params)
{
    LOG_FUNCTION_NAME;

    status_t rtn = NO_ERROR;

    // Udpate the current parameter set
    mParams = params;

    //check zoom value
    int zoom = mParams.getInt(CameraParameters::KEY_ZOOM);
    int maxzoom = mParams.getInt(CameraParameters::KEY_MAX_ZOOM);
    char *p = (char *)mParams.get(CameraParameters::KEY_ZOOM_RATIOS);

    if(zoom > maxzoom){
        rtn = INVALID_OPERATION;
        CAMHAL_LOGEB("Zoom Parameter Out of range1------zoom level:%d,max level:%d",zoom,maxzoom);
        zoom = maxzoom;
        mParams.set((const char*)CameraParameters::KEY_ZOOM, maxzoom);
    }else if(zoom <0) {
        rtn = INVALID_OPERATION;
        zoom = 0;
        CAMHAL_LOGEB("Zoom Parameter Out of range2------zoom level:%d,max level:%d",zoom,maxzoom);
        mParams.set((const char*)CameraParameters::KEY_ZOOM, zoom);
    }

    if ((p) && (zoom >= 0)&&(zoom!=mZoomlevel)) {
        int z = (int)strtol(p, &p, 10);
        int i = 0;
        while (i < zoom) {
            if (*p != ',') break;
            z = (int)strtol(p+1, &p, 10);
            i++;
        }
        notifyZoomSubscribers((mZoomlevel<0)?0:mZoomlevel,zoom);
        CAMHAL_LOGDB("Change the zoom level---old:%d,new:%d",mZoomlevel,zoom);
        mZoomlevel = zoom;
        SYS_set_zoom(z);
    }
 
    int min_fps,max_fps;
    const char *white_balance=NULL;
    const char *exposure=NULL;
    const char *effect=NULL;
    //const char *night_mode=NULL;
    const char *qulity=NULL;
    const char *banding=NULL;
    const char *flashmode=NULL;

    white_balance=mParams.get(CameraParameters::KEY_WHITE_BALANCE);
    exposure=mParams.get(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    effect=mParams.get(CameraParameters::KEY_EFFECT);
    banding=mParams.get(CameraParameters::KEY_ANTIBANDING);
    qulity=mParams.get(CameraParameters::KEY_JPEG_QUALITY);
    flashmode = mParams.get(CameraParameters::KEY_FLASH_MODE);
    if(flashmode)
        set_flash_mode(flashmode);
    if(exposure)
        SetExposure(mCameraHandle,exposure);
    if(white_balance)
        set_white_balance(mCameraHandle,white_balance);
    if(effect)
        set_effect(mCameraHandle,effect);
    if(banding)
        set_banding(mCameraHandle,banding);

    mParams.getPreviewFpsRange(&min_fps, &max_fps);
    if((min_fps<0)||(max_fps<0)||(max_fps<min_fps))
    {
        rtn = INVALID_OPERATION;
    }

    LOG_FUNCTION_NAME_EXIT;
    return rtn;
}


void V4LCameraAdapter::getParameters(CameraParameters& params)
{
    LOG_FUNCTION_NAME;

    // Return the current parameter set
    //params = mParams;
    //that won't work. we might wipe out the existing params

    LOG_FUNCTION_NAME_EXIT;
}


///API to give the buffers to Adapter
status_t V4LCameraAdapter::useBuffers(CameraMode mode, void* bufArr, int num, size_t length, unsigned int queueable)
{
    status_t ret = NO_ERROR;

    LOG_FUNCTION_NAME;

    Mutex::Autolock lock(mLock);

    switch(mode)
    {
        case CAMERA_PREVIEW:
            ret = UseBuffersPreview(bufArr, num);
            //maxQueueable = queueable;
            break;
        case CAMERA_IMAGE_CAPTURE:
            ret = UseBuffersCapture(bufArr, num);
            break;
        case CAMERA_VIDEO:
            //@warn Video capture is not fully supported yet
            ret = UseBuffersPreview(bufArr, num);
            //maxQueueable = queueable;
            break;
    }

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t V4LCameraAdapter::setBuffersFormat(int width, int height, int pixelformat)
{
    int ret = NO_ERROR;
    CAMHAL_LOGDB("Width * Height %d x %d format 0x%x", width, height, pixelformat);

    mVideoInfo->width = width;
    mVideoInfo->height = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn = pixelformat;

    mVideoInfo->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->format.fmt.pix.width = width;
    mVideoInfo->format.fmt.pix.height = height;
    mVideoInfo->format.fmt.pix.pixelformat = pixelformat;

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &mVideoInfo->format);
    if (ret < 0) {
        CAMHAL_LOGEB("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        LOGD("ret=%d", ret);
        return ret;
    }

    return ret;
}

status_t V4LCameraAdapter::getBuffersFormat(int &width, int &height, int &pixelformat)
{
    int ret = NO_ERROR;
    struct v4l2_format format;
	
    memset(&format, 0,sizeof(struct v4l2_format));

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mCameraHandle, VIDIOC_G_FMT, &format);
    if (ret < 0) {
        CAMHAL_LOGEB("Open: VIDIOC_G_FMT Failed: %s", strerror(errno));
        LOGD("ret=%d", ret);
        return ret;
    }
    width = format.fmt.pix.width;
    height = format.fmt.pix.height;
    pixelformat = format.fmt.pix.pixelformat;
    CAMHAL_LOGDB("Get BufferFormat Width * Height %d x %d format 0x%x", width, height, pixelformat);	
    return ret;
}

status_t V4LCameraAdapter::UseBuffersPreview(void* bufArr, int num)
{
    int ret = NO_ERROR;

    if(NULL == bufArr)
    {
        return BAD_VALUE;
    }

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    if((mUsbCameraStatus == USBCAMERA_ACTIVED)||(mUsbCameraStatus == USBCAMERA_NO_INIT)){
        if(mCameraHandle>=0)
            close(mCameraHandle);

        mUsbCameraStatus = USBCAMERA_NO_INIT;

        if ((mCameraHandle = open(DEVICE_PATH(mSensorIndex), O_RDWR)) == -1)
        {
            CAMHAL_LOGEB("Error while opening handle to V4L2 Camera: %s", strerror(errno));
            return -EINVAL;
        }

        ret = ioctl (mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
        if (ret < 0)
        {
            CAMHAL_LOGEA("Error when querying the capabilities of the V4L Camera");
            return -EINVAL;
        }

        if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
        {
            CAMHAL_LOGEA("Error while adapter initialization: video capture not supported.");
            return -EINVAL;
        }

        if (!(mVideoInfo->cap.capabilities & V4L2_CAP_STREAMING))
        {
            CAMHAL_LOGEA("Error while adapter initialization: Capture device does not support streaming i/o");
            return -EINVAL;
        }
        mUsbCameraStatus = USBCAMERA_INITED;
        mVideoInfo->isStreaming = false;
    }
#endif

    int width, height;
    mParams.getPreviewSize(&width, &height);
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    setBuffersFormat(width, height, V4L2_PIX_FMT_YUYV);
#else
    setBuffersFormat(width, height, DEFAULT_PREVIEW_PIXEL_FORMAT);
#endif
    //First allocate adapter internal buffers at V4L level for USB Cam
    //These are the buffers from which we will copy the data into overlay buffers
    /* Check if camera can handle NB_BUFFER buffers */
    mVideoInfo->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->rb.memory = V4L2_MEMORY_MMAP;
    mVideoInfo->rb.count = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
    if (ret < 0) {
        CAMHAL_LOGEB("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < num; i++) {

        memset (&mVideoInfo->buf, 0, sizeof (struct v4l2_buffer));

        mVideoInfo->buf.index = i;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl (mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
        if (ret < 0) {
            CAMHAL_LOGEB("Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        mVideoInfo->mem[i] = mmap (0,
               mVideoInfo->buf.length,
               PROT_READ | PROT_WRITE,
               MAP_SHARED,
               mCameraHandle,
               mVideoInfo->buf.m.offset);

        if (mVideoInfo->mem[i] == MAP_FAILED) {
            CAMHAL_LOGEB("Unable to map buffer (%s)", strerror(errno));
            return -1;
        }

        uint32_t *ptr = (uint32_t*) bufArr;

        //Associate each Camera internal buffer with the one from Overlay
        LOGD("mPreviewBufs.add %#x, %d", ptr[i], i);
        mPreviewBufs.add((int)ptr[i], i);

    }

    for(int i = 0;i < num; i++)
    {
        mPreviewIdxs.add(mPreviewBufs.valueAt(i),i);
    }

    // Update the preview buffer count
    mPreviewBufferCount = num;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    mUsbCameraStatus = USBCAMERA_ACTIVED;
#endif
    return ret;
}

status_t V4LCameraAdapter::UseBuffersCapture(void* bufArr, int num)
{
    int ret = NO_ERROR;

    if(NULL == bufArr)
    {
        return BAD_VALUE;
    }

    if (num != 1)
    {
        LOGD("----------------- UseBuffersCapture num=%d", num);
    }

    /* This will only be called right before taking a picture, so
     * stop preview now so that we can set buffer format here.
     */
    LOGD("UseBuffersCapture stopPreview..");
    this->stopPreview();

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    if((mUsbCameraStatus == USBCAMERA_ACTIVED)||(mUsbCameraStatus == USBCAMERA_NO_INIT)){
        if(mCameraHandle>=0)
            close(mCameraHandle);

        mUsbCameraStatus = USBCAMERA_NO_INIT;

        if ((mCameraHandle = open(DEVICE_PATH(mSensorIndex), O_RDWR)) == -1)
        {
            CAMHAL_LOGEB("Error while opening handle to V4L2 Camera: %s", strerror(errno));
            return -EINVAL;
        }

        ret = ioctl (mCameraHandle, VIDIOC_QUERYCAP, &mVideoInfo->cap);
        if (ret < 0)
        {
            CAMHAL_LOGEA("Error when querying the capabilities of the V4L Camera");
            return -EINVAL;
        }

        if ((mVideoInfo->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0)
        {
            CAMHAL_LOGEA("Error while adapter initialization: video capture not supported.");
            return -EINVAL;
        }

        if (!(mVideoInfo->cap.capabilities & V4L2_CAP_STREAMING))
        {
            CAMHAL_LOGEA("Error while adapter initialization: Capture device does not support streaming i/o");
            return -EINVAL;
        }
        mUsbCameraStatus = USBCAMERA_INITED;
        mVideoInfo->isStreaming = false;
    }
#endif

    LOGD("UseBuffersCapture setBuffersFormat..");
    int width, height;
    mParams.getPictureSize(&width, &height);
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    setBuffersFormat(width, height, V4L2_PIX_FMT_YUYV);
#else
    setBuffersFormat(width, height, DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT);
#endif

    //First allocate adapter internal buffers at V4L level for Cam
    //These are the buffers from which we will copy the data into display buffers
    /* Check if camera can handle NB_BUFFER buffers */
    mVideoInfo->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->rb.memory = V4L2_MEMORY_MMAP;
    mVideoInfo->rb.count = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
    if (ret < 0) {
        CAMHAL_LOGEB("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < num; i++) {

        memset (&mVideoInfo->buf, 0, sizeof (struct v4l2_buffer));

        mVideoInfo->buf.index = i;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl (mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
        if (ret < 0) {
            CAMHAL_LOGEB("Unable to query buffer (%s)", strerror(errno));
            return ret;
        }

        mVideoInfo->mem[i] = mmap (0,
               mVideoInfo->buf.length,
               PROT_READ | PROT_WRITE,
               MAP_SHARED,
               mCameraHandle,
               mVideoInfo->buf.m.offset);

        if (mVideoInfo->mem[i] == MAP_FAILED) {
            CAMHAL_LOGEB("Unable to map buffer (%s)", strerror(errno));
            return -1;
        }

        uint32_t *ptr = (uint32_t*) bufArr;
        LOGV("UseBuffersCapture %#x", ptr[0]);
        mCaptureBuf = (camera_memory_t*)ptr[0];
    }

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    mUsbCameraStatus = USBCAMERA_ACTIVED;
#endif
    return ret;
}

status_t V4LCameraAdapter::takePicture()
{
    LOG_FUNCTION_NAME;
    if (createThread(beginPictureThread, this) == false)
        return -1;
    LOG_FUNCTION_NAME_EXIT;
    return NO_ERROR;
}

status_t V4LCameraAdapter::startPreview()
{
    status_t ret = NO_ERROR;
    int frame_count = 0,ret_c = 0;
    void *frame_buf = NULL;
    Mutex::Autolock lock(mPreviewBufsLock);

    if(mPreviewing)
    {
        return BAD_VALUE;
    }

#ifndef AMLOGIC_USB_CAMERA_SUPPORT
    writefile(SYSFILE_CAMERA_SET_MIRROR,(char*)(mbFrontCamera?"1":"0"));
#endif

    nQueued = 0;
    for (int i = 0; i < mPreviewBufferCount; i++) 
    {
        frame_count = -1;
        frame_buf = (void *)mPreviewBufs.keyAt(i);

        if((ret_c = getFrameRefCount(frame_buf,CameraFrame::PREVIEW_FRAME_SYNC))>=0)
            frame_count = ret_c;

        //if((ret_c = getFrameRefCount(frame_buf,CameraFrame::VIDEO_FRAME_SYNC))>=0)
        //    frame_count += ret_c;
 
        CAMHAL_LOGDB("startPreview--buffer address:0x%x, refcount:%d",(uint32_t)frame_buf,frame_count);
        if(frame_count>0)
            continue;
        //mVideoInfo->buf.index = i;
        mVideoInfo->buf.index = mPreviewBufs.valueFor((uint32_t)frame_buf);
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
        if (ret < 0) {
            CAMHAL_LOGEA("VIDIOC_QBUF Failed");
            return -EINVAL;
        }
        CAMHAL_LOGDB("startPreview --length=%d, index:%d", mVideoInfo->buf.length,mVideoInfo->buf.index);
        nQueued++;
    }

    enum v4l2_buf_type bufType;
    if (!mVideoInfo->isStreaming) 
    {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret = ioctl (mCameraHandle, VIDIOC_STREAMON, &bufType);
        if (ret < 0) {
            CAMHAL_LOGEB("StartStreaming: Unable to start capture: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreaming = true;
    }

    // Create and start preview thread for receiving buffers from V4L Camera
    mPreviewThread = new PreviewThread(this);
    CAMHAL_LOGDA("Created preview thread");
    //Update the flag to indicate we are previewing
    mPreviewing = true;
    return ret;
}

status_t V4LCameraAdapter::stopPreview()
{
    enum v4l2_buf_type bufType;
    int ret = NO_ERROR;

    Mutex::Autolock lock(mPreviewBufsLock);

    if(!mPreviewing)
    {
	    return NO_INIT;
    }

    mPreviewing = false;
    mPreviewThread->requestExitAndWait();
    mPreviewThread.clear();


    LOGD("stopPreview streamoff..");
    if (mVideoInfo->isStreaming) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl (mCameraHandle, VIDIOC_STREAMOFF, &bufType);
        if (ret < 0) {
            CAMHAL_LOGEB("StopStreaming: Unable to stop capture: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreaming = false;
    }

    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

    nQueued = 0;
    nDequeued = 0;

    LOGD("stopPreview unmap..");
    /* Unmap buffers */
    for (int i = 0; i < mPreviewBufferCount; i++)
        if (munmap(mVideoInfo->mem[i], mVideoInfo->buf.length) < 0)
            CAMHAL_LOGEA("Unmap failed");

    LOGD("stopPreview clearexit..");
    mPreviewBufs.clear();
    mPreviewIdxs.clear();
    return ret;

}

char * V4LCameraAdapter::GetFrame(int &index)
{
    int ret;

    if(nQueued<=0){
        CAMHAL_LOGEA("GetFrame: No buff for Dequeue");
        return NULL;
    }
    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

    /* DQ */
    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &mVideoInfo->buf);
    if (ret < 0) {
        CAMHAL_LOGEA("GetFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    nDequeued++;
    nQueued--;
    index = mVideoInfo->buf.index;

    return (char *)mVideoInfo->mem[mVideoInfo->buf.index];
}

//API to get the frame size required  to be allocated. This size is used to override the size passed
//by camera service when VSTAB/VNF is turned ON for example
status_t V4LCameraAdapter::getFrameSize(size_t &width, size_t &height)
{
    status_t ret = NO_ERROR;

    // Just return the current preview size, nothing more to do here.
    mParams.getPreviewSize(( int * ) &width,
                           ( int * ) &height);

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t V4LCameraAdapter::getFrameDataSize(size_t &dataFrameSize, size_t bufferCount)
{
    // We don't support meta data, so simply return
    return NO_ERROR;
}

status_t V4LCameraAdapter::getPictureBufferSize(size_t &length, size_t bufferCount)
{
    int width, height;
    mParams.getPictureSize(&width, &height);
    if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_RGB24){ // rgb24
        length = width * height * 3;
    }else if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_YUYV){ //   422I
        length = width * height * 2;
    }else if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_NV21){
        length = width * height * 3/2;
    }else{
        length = width * height * 3;
    }
    return NO_ERROR;
}

static void debugShowFPS()
{
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps = ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        LOGD("Camera %d Frames, %f FPS", mFrameCount, mFps);
    }
    // XXX: mFPS has the value we want
}

status_t V4LCameraAdapter::recalculateFPS()
{
    float currentFPS;

    mFrameCount++;

    if ( ( mFrameCount % FPS_PERIOD ) == 0 )
    {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFPSTime;
        currentFPS =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFPSTime = now;
        mLastFrameCount = mFrameCount;

        if ( 1 == mIter )
        {
            mFPS = currentFPS;
        }
        else
        {
            //cumulative moving average
            mFPS = mLastFPS + (currentFPS - mLastFPS)/mIter;
        }

        mLastFPS = mFPS;
        mIter++;
    }

    return NO_ERROR;
}

void V4LCameraAdapter::onOrientationEvent(uint32_t orientation, uint32_t tilt)
{
    //LOG_FUNCTION_NAME;

    //LOG_FUNCTION_NAME_EXIT;
}


V4LCameraAdapter::V4LCameraAdapter(size_t sensor_index)
{
    LOG_FUNCTION_NAME;

    mSensorIndex = sensor_index;

    LOG_FUNCTION_NAME_EXIT;
}

V4LCameraAdapter::~V4LCameraAdapter()
{
    LOG_FUNCTION_NAME;

    // Close the camera handle and free the video info structure
    close(mCameraHandle);

    if (mVideoInfo)
    {
        free(mVideoInfo);
        mVideoInfo = NULL;
    }

#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    mUsbCameraStatus = USBCAMERA_NO_INIT;
#endif

    LOG_FUNCTION_NAME_EXIT;
}

/* Preview Thread */
// ---------------------------------------------------------------------------

int V4LCameraAdapter::previewThread()
{
    status_t ret = NO_ERROR;
    int width, height;
    CameraFrame frame;

    if (mPreviewing)
    {
        int index = 0;
        char *fp = this->GetFrame(index);
        if(!fp)
        {
            int previewFrameRate = mParams.getPreviewFrameRate();
            int delay = (int)(1000000.0f / float(previewFrameRate)) >> 1;
            CAMHAL_LOGEB("Preview thread get frame fail, need sleep:%d",delay);
            usleep(delay);
            return BAD_VALUE;
        }
        
        uint8_t* ptr = (uint8_t*) mPreviewBufs.keyAt(mPreviewIdxs.valueFor(index));

        if (!ptr)
        {
            CAMHAL_LOGEA("Preview thread mPreviewBufs error!");
            return BAD_VALUE;
        }

        uint8_t* dest = NULL;
#ifdef AMLOGIC_CAMERA_OVERLAY_SUPPORT
        camera_memory_t* VideoCameraBufferMemoryBase = (camera_memory_t*)ptr;
        dest = (uint8_t*)VideoCameraBufferMemoryBase->data; //ptr;
#else
        private_handle_t* gralloc_hnd = (private_handle_t*)ptr;
        dest = (uint8_t*)gralloc_hnd->base; //ptr;
#endif
        int width, height;
        uint8_t* src = (uint8_t*) fp;
        mParams.getPreviewSize(&width, &height);
        if(DEFAULT_PREVIEW_PIXEL_FORMAT == V4L2_PIX_FMT_YUYV){ // 422I
            frame.mLength = width*height*2;
            memcpy(dest,src,frame.mLength);
        }else if(DEFAULT_PREVIEW_PIXEL_FORMAT == V4L2_PIX_FMT_NV21){ //420sp
            frame.mLength = width*height*3/2;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
            //convert yuyv to nv21
            yuyv422_to_nv21(src,dest,width,height);
#else
            memcpy(dest,src,frame.mLength);
#endif
        }else{ //default case
            frame.mLength = width*height*3/2;
            memcpy(dest,src,frame.mLength);            
        }

        frame.mFrameMask |= CameraFrame::PREVIEW_FRAME_SYNC;
        
        if(mRecording){
            frame.mFrameMask |= CameraFrame::VIDEO_FRAME_SYNC;
        }
        frame.mBuffer = ptr; //dest
        frame.mAlignment = width;
        frame.mOffset = 0;
        frame.mYuv[0] = NULL;
        frame.mYuv[1] = NULL;
        frame.mWidth = width;
        frame.mHeight = height;
        frame.mTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        ret = setInitFrameRefCount(frame.mBuffer, frame.mFrameMask);
        if (ret)
            LOGE("setInitFrameRefCount err=%d", ret);
        else
            ret = sendFrameToSubscribers(&frame);
        //LOGD("previewThread /sendFrameToSubscribers ret=%d", ret);           
    }
    return ret;
}

/* Image Capture Thread */
// ---------------------------------------------------------------------------
int V4LCameraAdapter::GenExif(ExifElementsTable* exiftable)
{
    char exifcontent[256];

    //Make
    exiftable->insertElement("Make",(const char*)mParams.get(TICameraParameters::KEY_EXIF_MAKE));

    //Model
    exiftable->insertElement("Model",(const char*)mParams.get(TICameraParameters::KEY_EXIF_MODEL));

    //Image orientation
    int orientation = mParams.getInt(CameraParameters::KEY_ROTATION);
    //covert 0 90 180 270 to 0 1 2 3
    LOGE("get orientaion %d",orientation);
    if(orientation == 0)
        orientation = 1;
    else if(orientation == 90)
        orientation = 6;
    else if(orientation == 180)
        orientation = 3;
    else if(orientation == 270)
        orientation = 8;
    sprintf(exifcontent,"%d",orientation);
    LOGD("exifcontent %s",exifcontent);
    exiftable->insertElement("Orientation",(const char*)exifcontent);

    //Image width,height
    int width,height;
    mParams.getPictureSize(&width,&height);
    sprintf(exifcontent,"%d",width);
    exiftable->insertElement("ImageWidth",(const char*)exifcontent);
    sprintf(exifcontent,"%d",height);
    exiftable->insertElement("ImageLength",(const char*)exifcontent);

    //focal length  RATIONAL
    float focallen = mParams.getFloat(CameraParameters::KEY_FOCAL_LENGTH);
    if(focallen >= 0)
    {
        int focalNum = focallen*1000;
        int focalDen = 1000;
        sprintf(exifcontent,"%d/%d",focalNum,focalDen);
        exiftable->insertElement("FocalLength",(const char*)exifcontent);
    }

    //datetime of photo
    time_t times;
    {
        time_t curtime = 0;
        time(&curtime);
        struct tm tmstruct;
        tmstruct = *(localtime(&times)); //convert to local time

        //date&time
        strftime(exifcontent, 30, "%Y:%m:%d %H:%M:%S", &tmstruct);
        exiftable->insertElement("DateTime",(const char*)exifcontent);
    }

    //gps date stamp & time stamp
    times = mParams.getInt(CameraParameters::KEY_GPS_TIMESTAMP);
    if(times != -1)
    {
        struct tm tmstruct;
        tmstruct = *(gmtime(&times));//convert to standard time
        //date
        strftime(exifcontent, 20, "%Y:%m:%d", &tmstruct);
        exiftable->insertElement("GPSDateStamp",(const char*)exifcontent);
        //time
        sprintf(exifcontent,"%d/%d,%d/%d,%d/%d",tmstruct.tm_hour,1,tmstruct.tm_min,1,tmstruct.tm_sec,1);
        exiftable->insertElement("GPSTimeStamp",(const char*)exifcontent);
    }

    //gps latitude info
    char* latitudestr = (char*)mParams.get(CameraParameters::KEY_GPS_LATITUDE);
    if(latitudestr!=NULL)
    {
        int offset = 0;
        float latitude = mParams.getFloat(CameraParameters::KEY_GPS_LATITUDE);
        if(latitude < 0.0)
        {
            offset = 1;
            latitude*= (float)(-1);
        }

        int latitudedegree = latitude;
        float latitudeminuts = (latitude-(float)latitudedegree)*60;
        int latitudeminuts_int = latitudeminuts;
        float latituseconds = (latitudeminuts-(float)latitudeminuts_int)*60+0.5;
        int latituseconds_int = latituseconds;
        sprintf(exifcontent,"%d/%d,%d/%d,%d/%d",latitudedegree,1,latitudeminuts_int,1,latituseconds_int,1);
        exiftable->insertElement("GPSLatitude",(const char*)exifcontent);

        exiftable->insertElement("GPSLatitudeRef",(offset==1)?"S":"N");
    }

    //gps Longitude info
    char* longitudestr = (char*)mParams.get(CameraParameters::KEY_GPS_LONGITUDE);
    if(longitudestr!=NULL)
    {
        int offset = 0;
        float longitude = mParams.getFloat(CameraParameters::KEY_GPS_LONGITUDE);
        if(longitude < 0.0)
        {
            offset = 1;
            longitude*= (float)(-1);
        }

        int longitudedegree = longitude;
        float longitudeminuts = (longitude-(float)longitudedegree)*60;
        int longitudeminuts_int = longitudeminuts;
        float longitudeseconds = (longitudeminuts-(float)longitudeminuts_int)*60+0.5;
        int longitudeseconds_int = longitudeseconds;
        sprintf(exifcontent,"%d/%d,%d/%d,%d/%d",longitudedegree,1,longitudeminuts_int,1,longitudeseconds_int,1);
        exiftable->insertElement("GPSLongitude",(const char*)exifcontent);

        exiftable->insertElement("GPSLongitudeRef",(offset==1)?"S":"N");
    }

    //gps Altitude info
    char* altitudestr = (char*)mParams.get(CameraParameters::KEY_GPS_ALTITUDE);
    if(altitudestr!=NULL)
    {
        int offset = 0;
        float altitude = mParams.getFloat(CameraParameters::KEY_GPS_ALTITUDE);
        if(altitude < 0.0)
        {
            offset = 1;
            altitude*= (float)(-1);
        }

        int altitudenum = altitude*1000;
        int altitudedec= 1000;
        sprintf(exifcontent,"%d/%d",altitudenum,altitudedec);
        exiftable->insertElement("GPSAltitude",(const char*)exifcontent);

        sprintf(exifcontent,"%d",offset);
        exiftable->insertElement("GPSAltitudeRef",(const char*)exifcontent);
    }

    //gps processing method
    char* processmethod = (char*)mParams.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
    if(processmethod!=NULL)
    {
        memset(exifcontent,0,sizeof(exifcontent));
        char ExifAsciiPrefix[] = { 0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0 };//asicii
        memcpy(exifcontent,ExifAsciiPrefix,8);
        memcpy(exifcontent+8,processmethod,strlen(processmethod));
        exiftable->insertElement("GPSProcessingMethod",(const char*)exifcontent);
    }
    return 1;
}

/*static*/ int V4LCameraAdapter::beginPictureThread(void *cookie)
{
    V4LCameraAdapter *c = (V4LCameraAdapter *)cookie;
    return c->pictureThread();
}

int V4LCameraAdapter::pictureThread()
{
    status_t ret = NO_ERROR;
    int width, height;
    CameraFrame frame;

#ifndef AMLOGIC_USB_CAMERA_SUPPORT
    writefile(SYSFILE_CAMERA_SET_MIRROR,(char*)(mbFrontCamera?"1":"0"));
#endif

    if (true)
    {
        mVideoInfo->buf.index = 0;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
        if (ret < 0) 
        {
            CAMHAL_LOGEA("VIDIOC_QBUF Failed");
            return -EINVAL;
        }

        enum v4l2_buf_type bufType;
        if (!mVideoInfo->isStreaming) 
        {
            bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            ret = ioctl (mCameraHandle, VIDIOC_STREAMON, &bufType);
            if (ret < 0) {
                CAMHAL_LOGEB("StartStreaming: Unable to start capture: %s", strerror(errno));
                return ret;
            }

            mVideoInfo->isStreaming = true;
        }
        nQueued ++;
        int index = 0;
        char *fp = this->GetFrame(index);
        if(!fp)
        {
	        return 0; //BAD_VALUE;
        }

        if (!mCaptureBuf || !mCaptureBuf->data)
        {
	        return 0; //BAD_VALUE;
        }

        int width, height;
        uint8_t* dest = (uint8_t*)mCaptureBuf->data;
        uint8_t* src = (uint8_t*) fp;
        mParams.getPictureSize(&width, &height);
        LOGD("pictureThread mCaptureBuf=%#x dest=%#x fp=%#x width=%d height=%d", mCaptureBuf, dest, fp, width, height);
        LOGD("length=%d bytesused=%d index=%d", mVideoInfo->buf.length, mVideoInfo->buf.bytesused, index);

        if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_RGB24){ // rgb24
            frame.mLength = width*height*3;
            frame.mQuirks = CameraFrame::ENCODE_RAW_RGB24_TO_JPEG | CameraFrame::HAS_EXIF_DATA;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
            //convert yuyv to rgb24
            yuyv422_to_rgb24(src,dest,width,height);
#else
            memcpy(dest,src,mVideoInfo->buf.length);
#endif
        }else if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_YUYV){ //   422I
            frame.mLength = width*height*2;
            frame.mQuirks = CameraFrame::ENCODE_RAW_YUV422I_TO_JPEG | CameraFrame::HAS_EXIF_DATA;
            memcpy(dest, src, mVideoInfo->buf.length);
        }else if(DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT == V4L2_PIX_FMT_NV21){ //   420sp
            frame.mLength = width*height*3/2;
            frame.mQuirks = CameraFrame::ENCODE_RAW_YUV420SP_TO_JPEG | CameraFrame::HAS_EXIF_DATA;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
            //convert yuyv to nv21
            yuyv422_to_nv21(src,dest,width,height);
#else
            memcpy(dest,src,mVideoInfo->buf.length);
#endif
        }else{ //default case
            frame.mLength = width*height*3;
            frame.mQuirks = CameraFrame::ENCODE_RAW_RGB24_TO_JPEG | CameraFrame::HAS_EXIF_DATA;
            memcpy(dest, src, mVideoInfo->buf.length);
        }

        notifyShutterSubscribers();
        //TODO correct time to call this?
        if (NULL != mEndImageCaptureCallback)
            mEndImageCaptureCallback(mEndCaptureData);

        //gen  exif message
        ExifElementsTable* exiftable = new ExifElementsTable();
        GenExif(exiftable);

        frame.mFrameMask = CameraFrame::IMAGE_FRAME;
        frame.mFrameType = CameraFrame::IMAGE_FRAME;
        frame.mBuffer = mCaptureBuf->data;
        frame.mCookie2 = (void*)exiftable;
        frame.mAlignment = width;
        frame.mOffset = 0;
        frame.mYuv[0] = NULL;
        frame.mYuv[1] = NULL;
        frame.mWidth = width;
        frame.mHeight = height;
        frame.mTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        
        if (mVideoInfo->isStreaming) 
        {
            bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ret = ioctl (mCameraHandle, VIDIOC_STREAMOFF, &bufType);
            if (ret < 0) 
            {
                CAMHAL_LOGEB("StopStreaming: Unable to stop capture: %s", strerror(errno));
                return ret;
            }

            mVideoInfo->isStreaming = false;
        }

        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

        nQueued = 0;
        nDequeued = 0;

        /* Unmap buffers */
        if (munmap(mVideoInfo->mem[0], mVideoInfo->buf.length) < 0)
            CAMHAL_LOGEA("Unmap failed");
    }

    // start preview thread again after stopping it in UseBuffersCapture
    {
        Mutex::Autolock lock(mPreviewBufferLock);
        UseBuffersPreview(mPreviewBuffers, mPreviewBufferCount);        
    }
    startPreview();

    ret = setInitFrameRefCount(frame.mBuffer, frame.mFrameMask);
    if (ret)
        LOGE("setInitFrameRefCount err=%d", ret);
    else
        ret = sendFrameToSubscribers(&frame);
    //LOGD("pictureThread /sendFrameToSubscribers ret=%d", ret);

    return ret;
}


// ---------------------------------------------------------------------------
extern "C" CameraAdapter* CameraAdapter_Factory(size_t sensor_index)
{
    CameraAdapter *adapter = NULL;
    Mutex::Autolock lock(gAdapterLock);

    LOG_FUNCTION_NAME;

    adapter = new V4LCameraAdapter(sensor_index);
    if ( adapter ) {
        CAMHAL_LOGDB("New V4L Camera adapter instance created for sensor %d", sensor_index);
    } else {
        CAMHAL_LOGEA("Camera adapter create failed!");
    }

    LOG_FUNCTION_NAME_EXIT;

    return adapter;
}

extern "C" int CameraAdapter_Capabilities(CameraProperties::Properties* properties_array,
                                          const unsigned int starting_camera,
                                          const unsigned int camera_num) {
    int num_cameras_supported = 0;
    CameraProperties::Properties* properties = NULL;

    LOG_FUNCTION_NAME;

    if(!properties_array)
    {
        return -EINVAL;
    }

    while (starting_camera + num_cameras_supported < camera_num)
    {
        properties = properties_array + starting_camera + num_cameras_supported;
        properties->set(CameraProperties::CAMERA_NAME, "Camera");
        extern void loadCaps(int camera_id, CameraProperties::Properties* params);
        loadCaps(starting_camera + num_cameras_supported, properties);
        num_cameras_supported++;
    }

    LOG_FUNCTION_NAME_EXIT;

    return num_cameras_supported;
}

static int iCamerasNum = -1;
extern "C"  int CameraAdapter_CameraNum()
{
#if defined(AMLOGIC_FRONT_CAMERA_SUPPORT) || defined(AMLOGIC_BACK_CAMERA_SUPPORT) ||defined(AMLOGIC_USB_CAMERA_SUPPORT)
    LOGD("CameraAdapter_CameraNum %d",MAX_CAMERAS_SUPPORTED);
    return MAX_CAMERAS_SUPPORTED;
#else
    LOGD("CameraAdapter_CameraNum %d",iCamerasNum);
    if(iCamerasNum == -1) 
    {
        iCamerasNum = 0;
        for(int i = 0;i < MAX_CAMERAS_SUPPORTED;i++)
        {
            if( access(DEVICE_PATH(i), 0) == 0 )
            {
                iCamerasNum++;
            }
        }
        LOGD("GetCameraNums %d",iCamerasNum);
    }

    return iCamerasNum;
#endif
}


extern "C" int getValidFrameSize(int camera_id, int pixel_format, char *framesize)
{
    struct v4l2_frmsizeenum frmsize;
    int fd, i=0;
    char tempsize[12];
    framesize[0] = '\0';
    fd = open(DEVICE_PATH(camera_id), O_RDWR);
    if (fd >= 0) {
        memset(&frmsize,0,sizeof(v4l2_frmsizeenum));
        for(i=0;;i++){
            frmsize.index = i;
            frmsize.pixel_format = pixel_format;
            if(ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0){
                if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){ //only support this type
                    snprintf(tempsize, sizeof(tempsize), "%dx%d,",
                            frmsize.discrete.width, frmsize.discrete.height);
                    strcat(framesize, tempsize);
                }
                else
                    break;
            }
            else
                break;
        }
        close(fd);
    }
    if(framesize[0] == '\0')
        return -1;
    else
        return 0;
}

static int getCameraOrientation(bool frontcamera, char* property)
{
    int degree = -1;
    if(frontcamera){
        if (property_get("ro.camera.orientation.front", property, NULL) > 0){
            degree = atoi(property);
        }
    }else{
        if (property_get("ro.camera.orientation.back", property, NULL) > 0){
            degree = atoi(property);
        }
    }
    if((degree != 0)&&(degree != 90)
      &&(degree != 180)&&(degree != 270))
        degree = -1;
    return degree;
}

//TODO move
extern "C" void loadCaps(int camera_id, CameraProperties::Properties* params) {
    const char DEFAULT_BRIGHTNESS[] = "50";
    const char DEFAULT_CONTRAST[] = "100";
    const char DEFAULT_IPP[] = "ldc-nsf";
    const char DEFAULT_GBCE[] = "disable";
    const char DEFAULT_ISO_MODE[] = "auto";
    const char DEFAULT_PICTURE_FORMAT[] = "jpeg";
    const char DEFAULT_PICTURE_SIZE[] = "640x480";
    const char PREVIEW_FORMAT_420SP[] = "yuv420sp";
    const char PREVIEW_FORMAT_422I[] = "yuv422i-yuyv";
    const char DEFAULT_PREVIEW_SIZE[] = "640x480";
    const char DEFAULT_NUM_PREV_BUFS[] = "6";
    const char DEFAULT_NUM_PIC_BUFS[] = "1";
    const char DEFAULT_MAX_FOCUS_AREAS[] = "1";
    const char DEFAULT_SATURATION[] = "100";
    const char DEFAULT_SCENE_MODE[] = "auto";
    const char DEFAULT_SHARPNESS[] = "100";
    const char DEFAULT_VSTAB[] = "false";
    const char DEFAULT_VSTAB_SUPPORTED[] = "true";
    const char DEFAULT_MAX_FD_HW_FACES[] = "0";
    const char DEFAULT_MAX_FD_SW_FACES[] = "0";
    const char DEFAULT_FOCAL_LENGTH_PRIMARY[] = "4.31";
    const char DEFAULT_FOCAL_LENGTH_SECONDARY[] = "1.95";
    const char DEFAULT_HOR_ANGLE[] = "54.8";
    const char DEFAULT_VER_ANGLE[] = "42.5";
    const char DEFAULT_AE_LOCK[] = "false";
    const char DEFAULT_AWB_LOCK[] = "false";
    const char DEFAULT_MAX_NUM_METERING_AREAS[] = "0";
    const char DEFAULT_LOCK_SUPPORTED[] = "true";
    const char DEFAULT_LOCK_UNSUPPORTED[] = "false";
    const char DEFAULT_VIDEO_SNAPSHOT_SUPPORTED[] = "true";
    const char DEFAULT_VIDEO_SIZE[] = "640x480";
    const char DEFAULT_PREFERRED_PREVIEW_SIZE_FOR_VIDEO[] = "640x480";

    bool bFrontCam = false;
    if (camera_id == 0) {
#ifdef AMLOGIC_BACK_CAMERA_SUPPORT
        bFrontCam = false;
#elif defined(AMLOGIC_FRONT_CAMERA_SUPPORT)
        bFrontCam = true;
#elif defined(AMLOGIC_USB_CAMERA_SUPPORT)
        bFrontCam = true;
#else//defined nothing, we try by ourself
        if(CameraAdapter_CameraNum() > 1) { //when have more than one cameras, this 0 is backcamera
            bFrontCam = false;
        } else {
            bFrontCam = true;
        }
#endif
    } else if (camera_id == 1) {
#if defined(AMLOGIC_BACK_CAMERA_SUPPORT) && defined(AMLOGIC_FRONT_CAMERA_SUPPORT)
        bFrontCam = true;
#else//defined nothing, we try  to by ourself
        if(CameraAdapter_CameraNum() > 1) { //when have more than one cameras, this 1 is frontcamera
            bFrontCam = true;
        } else {
            LOGE("Should not run to here when just have 1 camera");
        }
#endif
    }

    //should changed while the screen orientation changed.
    int degree = -1;
    char property[64];
    memset(property,0,sizeof(property));
    if(bFrontCam == true) {
        params->set(CameraProperties::FACING_INDEX, TICameraParameters::FACING_FRONT);
        if(getCameraOrientation(bFrontCam,property)>=0){
            params->set(CameraProperties::ORIENTATION_INDEX,property);
        }else{
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
            params->set(CameraProperties::ORIENTATION_INDEX,"0");
#else
            params->set(CameraProperties::ORIENTATION_INDEX,"270");
#endif
        }
    } else {
        params->set(CameraProperties::FACING_INDEX, TICameraParameters::FACING_BACK);
        if(getCameraOrientation(bFrontCam,property)>=0){
            params->set(CameraProperties::ORIENTATION_INDEX,property);
        }else{
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
            params->set(CameraProperties::ORIENTATION_INDEX,"180");
#else
            params->set(CameraProperties::ORIENTATION_INDEX,"90");
#endif
        }
    }

    params->set(CameraProperties::SUPPORTED_PREVIEW_FORMATS,"yuv420sp,yuv420p"); //yuv420p for cts
    if(DEFAULT_PREVIEW_PIXEL_FORMAT == V4L2_PIX_FMT_YUYV){ // 422I
        //params->set(CameraProperties::SUPPORTED_PREVIEW_FORMATS,PREVIEW_FORMAT_422I);
        params->set(CameraProperties::PREVIEW_FORMAT,PREVIEW_FORMAT_422I);
    }else if(DEFAULT_PREVIEW_PIXEL_FORMAT == V4L2_PIX_FMT_NV21){ //420sp
        //params->set(CameraProperties::SUPPORTED_PREVIEW_FORMATS,PREVIEW_FORMAT_420SP);
        params->set(CameraProperties::PREVIEW_FORMAT,PREVIEW_FORMAT_420SP);
    }else{ //default case
        //params->set(CameraProperties::SUPPORTED_PREVIEW_FORMATS,PREVIEW_FORMAT_420SP);
        params->set(CameraProperties::PREVIEW_FORMAT,PREVIEW_FORMAT_420SP);
    }

    params->set(CameraProperties::SUPPORTED_PREVIEW_FRAME_RATES, "10,15");
    params->set(CameraProperties::PREVIEW_FRAME_RATE, "15");

    params->set(CameraProperties::FRAMERATE_RANGE_SUPPORTED, "(8000,26623)");
    params->set(CameraProperties::FRAMERATE_RANGE, "8000,26623");
    params->set(CameraProperties::FRAMERATE_RANGE_IMAGE, "10000,15000");
    params->set(CameraProperties::FRAMERATE_RANGE_VIDEO, "10000,15000");

	//get preview size & set
    char *sizes = (char *) calloc (1, 1024);
    if(!sizes){
        CAMHAL_LOGEA("Alloc string buff error!");
        return;
    }        
    memset(sizes,0,1024);
    uint32_t preview_format = DEFAULT_PREVIEW_PIXEL_FORMAT;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    preview_format = V4L2_PIX_FMT_YUYV;
#endif
    if (!getValidFrameSize(camera_id, preview_format, sizes)) {
        int len = strlen(sizes);
        unsigned int supported_w = 0,  supported_h = 0,w = 0,h = 0;
        if(len>1){
            if(sizes[len-1] == ',')
                sizes[len-1] = '\0';
        }

#ifdef AML_CAMERA_BY_VM_INTERFACE
        char small_size[8] = "176x144"; //for cts
        if(strstr(sizes,small_size)==NULL){
            if((len+sizeof(small_size))<(1024-1)){
                strcat(sizes,",");
                strcat(sizes,small_size);
            }
        }
#endif
        params->set(CameraProperties::SUPPORTED_PREVIEW_SIZES, sizes);

        char * b = (char *)sizes;
        while(b != NULL){
            if (sscanf(b, "%dx%d", &supported_w, &supported_h) != 2){
                break;
            }
            if((supported_w*supported_h)>(w*h)){
                w = supported_w;
                h = supported_h;
            }
            b = strchr(b, ',');
            if(b)
                b++;
        }
        if((w>0)&&(h>0)){
            memset(sizes, 0, 1024);
            sprintf(sizes,"%dx%d",w,h);
        }
        //char * b = strrchr(sizes, ',');
        //if (b) 
        //    b++;
        //else 
        //    b = sizes;
        params->set(CameraProperties::PREVIEW_SIZE, sizes);
    }
    else
    {
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
        params->set(CameraProperties::SUPPORTED_PREVIEW_SIZES, "320x240,176x144,160x120");
        params->set(CameraProperties::PREVIEW_SIZE,"320x240");
#else
        params->set(CameraProperties::SUPPORTED_PREVIEW_SIZES, "640x480,352x288,176x144");
        params->set(CameraProperties::PREVIEW_SIZE,"640x480");
#endif
    }

    params->set(CameraProperties::SUPPORTED_PICTURE_FORMATS, DEFAULT_PICTURE_FORMAT);
    params->set(CameraProperties::PICTURE_FORMAT,DEFAULT_PICTURE_FORMAT);
    params->set(CameraProperties::JPEG_QUALITY, 90);

    //must have >2 sizes and contain "0x0"
    params->set(CameraProperties::SUPPORTED_THUMBNAIL_SIZES, "180x160,0x0");
    params->set(CameraProperties::JPEG_THUMBNAIL_SIZE, "180x160");
    params->set(CameraProperties::JPEG_THUMBNAIL_QUALITY, 90);

    //get & set picture size
    memset(sizes,0,1024);
    uint32_t picture_format = DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT;
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    picture_format = V4L2_PIX_FMT_YUYV;
#endif
    if (!getValidFrameSize(camera_id, picture_format, sizes)) {
        int len = strlen(sizes);
        unsigned int supported_w = 0,  supported_h = 0,w = 0,h = 0;
        if(len>1){
            if(sizes[len-1] == ',')
                sizes[len-1] = '\0';
        }

        params->set(CameraProperties::SUPPORTED_PICTURE_SIZES, sizes);

        char * b = (char *)sizes;
        while(b != NULL){
            if (sscanf(b, "%dx%d", &supported_w, &supported_h) != 2){
                break;
            }
            if((supported_w*supported_h)>(w*h)){
                w = supported_w;
                h = supported_h;
            }
            b = strchr(b, ',');
            if(b)
                b++;
        }
        if((w>0)&&(h>0)){
            memset(sizes, 0, 1024);
            sprintf(sizes,"%dx%d",w,h);
        }
        //char * b = strrchr(sizes, ',');
        //if (b) 
        //    b++;
        //else 
        //    b = sizes;
        params->set(CameraProperties::PICTURE_SIZE, sizes);
    } 
    else
    {
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
        params->set(CameraProperties::SUPPORTED_PICTURE_SIZES, "320x240");
        params->set(CameraProperties::PICTURE_SIZE,"320x240");
#else
        params->set(CameraProperties::SUPPORTED_PICTURE_SIZES, "640x480");
        params->set(CameraProperties::PICTURE_SIZE,"640x480");
#endif
    }
    free(sizes);

    params->set(CameraProperties::SUPPORTED_FOCUS_MODES, "fixed");
    params->set(CameraProperties::FOCUS_MODE, "fixed");

    params->set(CameraProperties::SUPPORTED_ANTIBANDING, "50hz,60hz");
    params->set(CameraProperties::ANTIBANDING, "50hz");

    params->set(CameraProperties::FOCAL_LENGTH, "4.31");

    params->set(CameraProperties::HOR_ANGLE,"54.8");
    params->set(CameraProperties::VER_ANGLE,"42.5");

    params->set(CameraProperties::SUPPORTED_WHITE_BALANCE, "auto,daylight,incandescent,fluorescent");
    params->set(CameraProperties::WHITEBALANCE, "auto");
    params->set(CameraProperties::AUTO_WHITEBALANCE_LOCK, DEFAULT_AWB_LOCK);

    params->set(CameraProperties::SUPPORTED_EFFECTS, "none,negative,sepia");
    params->set(CameraProperties::EFFECT, "none");
    if( access(FLASHLIGHT_PATH, 0) == 0 ){
        params->set(CameraProperties::SUPPORTED_FLASH_MODES, "on,off,torch");
        params->set(CameraProperties::FLASH_MODE, "on");
    }

    //params->set(CameraParameters::KEY_SUPPORTED_SCENE_MODES,"auto,night,snow");
    //params->set(CameraParameters::KEY_SCENE_MODE,"auto");

    params->set(CameraProperties::EXPOSURE_MODE, "auto");
    params->set(CameraProperties::SUPPORTED_EXPOSURE_MODES, "auto");
    params->set(CameraProperties::AUTO_EXPOSURE_LOCK, DEFAULT_AE_LOCK);

    params->set(CameraProperties::SUPPORTED_EV_MAX, 4);
    params->set(CameraProperties::SUPPORTED_EV_MIN, -4);
    params->set(CameraProperties::EV_COMPENSATION, 0);
    params->set(CameraProperties::SUPPORTED_EV_STEP, 1);

    //don't support digital zoom now
#ifdef AML_CAMERA_BY_VM_INTERFACE
    params->set(CameraProperties::ZOOM_SUPPORTED,"true");
    params->set(CameraProperties::SMOOTH_ZOOM_SUPPORTED,"false");
    params->set(CameraProperties::SUPPORTED_ZOOM_RATIOS,"100,120,140,160,180,200,220,280,300");
    params->set(CameraProperties::SUPPORTED_ZOOM_STAGES,8);	//think the zoom ratios as a array, the max zoom is the max index
    params->set(CameraProperties::ZOOM, 0);//default should be 0
#else
    params->set(CameraProperties::ZOOM_SUPPORTED,"false");
    params->set(CameraProperties::SMOOTH_ZOOM_SUPPORTED,"false");
    params->set(CameraProperties::SUPPORTED_ZOOM_RATIOS,"100");
    params->set(CameraProperties::SUPPORTED_ZOOM_STAGES,0);	//think the zoom ratios as a array, the max zoom is the max index
    params->set(CameraProperties::ZOOM, 0);//default should be 0
#endif

    params->set(CameraProperties::SUPPORTED_ISO_VALUES, "auto");
    params->set(CameraProperties::ISO_MODE, DEFAULT_ISO_MODE);

    params->set(CameraProperties::SUPPORTED_IPP_MODES, DEFAULT_IPP);
    params->set(CameraProperties::IPP, DEFAULT_IPP);

    params->set(CameraProperties::SUPPORTED_SCENE_MODES, "auto");
    params->set(CameraProperties::SCENE_MODE, DEFAULT_SCENE_MODE);

    params->set(CameraProperties::BRIGHTNESS, DEFAULT_BRIGHTNESS);
    params->set(CameraProperties::CONTRAST, DEFAULT_CONTRAST);
    params->set(CameraProperties::GBCE, DEFAULT_GBCE);
    params->set(CameraProperties::SATURATION, DEFAULT_SATURATION);
    params->set(CameraProperties::SHARPNESS, DEFAULT_SHARPNESS);
    params->set(CameraProperties::VSTAB, DEFAULT_VSTAB);
    params->set(CameraProperties::VSTAB_SUPPORTED, DEFAULT_VSTAB_SUPPORTED);
    params->set(CameraProperties::MAX_FD_HW_FACES, DEFAULT_MAX_FD_HW_FACES);
    params->set(CameraProperties::MAX_FD_SW_FACES, DEFAULT_MAX_FD_SW_FACES);
    params->set(CameraProperties::REQUIRED_PREVIEW_BUFS, DEFAULT_NUM_PREV_BUFS);
    params->set(CameraProperties::REQUIRED_IMAGE_BUFS, DEFAULT_NUM_PIC_BUFS);
    params->set(CameraProperties::VIDEO_SNAPSHOT_SUPPORTED, DEFAULT_VIDEO_SNAPSHOT_SUPPORTED);
#ifdef AMLOGIC_USB_CAMERA_SUPPORT
    params->set(CameraProperties::VIDEO_SIZE,params->get(CameraProperties::PREVIEW_SIZE));
    params->set(CameraProperties::PREFERRED_PREVIEW_SIZE_FOR_VIDEO,params->get(CameraProperties::PREVIEW_SIZE));
#else
    params->set(CameraProperties::VIDEO_SIZE, DEFAULT_VIDEO_SIZE);
    params->set(CameraProperties::PREFERRED_PREVIEW_SIZE_FOR_VIDEO, DEFAULT_PREFERRED_PREVIEW_SIZE_FOR_VIDEO);
#endif
}

extern "C" int set_white_balance(int camera_fd,const char *swb)
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

    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0)
        CAMHAL_LOGEB("AMLOGIC CAMERA Set white balance fail: %s. ret=%d", strerror(errno),ret);
    return ret ;
}

extern "C" int SetExposure(int camera_fd,const char *sbn)
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

    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0)
        CAMHAL_LOGEB("AMLOGIC CAMERA Set Exposure fail: %s. ret=%d", strerror(errno),ret);

    return ret ;
}

extern "C" int set_effect(int camera_fd,const char *sef)
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
    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0)
        CAMHAL_LOGEB("AMLOGIC CAMERA Set effect fail: %s. ret=%d", strerror(errno),ret);
     return ret ;
}

extern "C" int set_night_mode(int camera_fd,const char *snm)
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

    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0)
        CAMHAL_LOGEB("AMLOGIC CAMERA Set night mode fail: %s. ret=%d", strerror(errno),ret);
     return ret ;
}

extern "C" int set_banding(int camera_fd,const char *snm)
{
    int ret = 0;
    struct v4l2_control ctl;
    if(camera_fd<0)
        return -1;

    if(strcasecmp(snm,"50hz")==0)
        ctl.value=CAM_NM_AUTO;
    else if(strcasecmp(snm,"60hz")==0)
        ctl.value=CAM_NM_ENABLE;

    ctl.id = V4L2_CID_WHITENESS;

    ret = ioctl(camera_fd, VIDIOC_S_CTRL, &ctl);
    if(ret<0)
        CAMHAL_LOGEB("AMLOGIC CAMERA Set banding fail: %s. ret=%d", strerror(errno),ret);
    return ret ;
}

extern "C" int get_flash_mode(void)
{
    int value = 0;
    FILE* fp = NULL;
    fp = fopen("/sys/class/flashlight/flashlightflag","r");
    if(fp == NULL){
        LOGE("open file fail\n");
        return -1;
    }
    value=fgetc(fp);
    fclose(fp);
    return value-'0';
}

extern "C" int set_flash_mode(const char *sfm)
{
    int value = 0;
    FILE* fp = NULL;
    if(strcasecmp(sfm,"auto")==0)
        value=FLASHLIGHT_AUTO;
    else if(strcasecmp(sfm,"on")==0)
        value=FLASHLIGHT_ON;
    else if(strcasecmp(sfm,"off")==0)
        value=FLASHLIGHT_OFF;
    else if(strcasecmp(sfm,"off")==0)
        value=FLASHLIGHT_TORCH;
    else
        value=FLASHLIGHT_OFF;
    fp = fopen("/sys/class/flashlight/flashlightflag","w");
    if(fp == NULL){
        LOGE("open file fail\n");
        return -1;
    }
    fputc((int)(value+'0'),fp);
    fclose(fp);
    if(value == FLASHLIGHT_TORCH)//open flashlight immediately
        set_flash(true);
    else if(value == FLASHLIGHT_OFF)
        set_flash(false);
    return 0 ;
}

extern "C" int set_flash(bool mode)
{   
    int flag = 0;
    FILE* fp = NULL;
    if(mode){
        flag = get_flash_mode();
        if(flag == FLASHLIGHT_OFF ||flag == FLASHLIGHT_AUTO)//handle AUTO case on camera driver
            return 0;
        else if(flag == -1)
            return -1;
    }
    fp = fopen(FLASHLIGHT_PATH,"w");
    if(fp == NULL){
        LOGE("open file fail\n"); 
        return -1;
        }
    fputc((int)(mode+'0'),fp);
    fclose(fp);
    return 0;
}

};


/*--------------------Camera Adapter Class ENDS here-----------------------------*/

