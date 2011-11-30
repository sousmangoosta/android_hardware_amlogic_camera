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

#include <cutils/properties.h>

//for private_handle_t TODO move out of private header
#include <gralloc_priv.h>

#define UNLIKELY( exp ) (__builtin_expect( (exp) != 0, false ))
static int mDebugFps = 0;

#define Q16_OFFSET 16

#define HERE(Msg) {CAMHAL_LOGEB("--===line %d, %s===--\n", __LINE__, Msg);}

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
const char *device = DEVICE;

/*--------------------junk STARTS here-----------------------------*/
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

    if ((mCameraHandle = open(device, O_RDWR)) == -1)
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

    // Initialize flags
    mPreviewing = false;
    mVideoInfo->isStreaming = false;
    mRecording = false;

    // ---------
    writefile((char*)SYSFILE_CAMERA_SET_PARA, (char*)"1");
    writefile((char*)SYSFILE_CAMERA_SET_MIRROR, (char*)"1");

    LOG_FUNCTION_NAME_EXIT;

    return ret;
}

status_t V4LCameraAdapter::fillThisBuffer(void* frameBuf, CameraFrame::FrameType frameType)
{

    status_t ret = NO_ERROR;

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

    mVideoInfo->buf.index = i;
    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
    if (ret < 0) {
       CAMHAL_LOGEA("Init: VIDIOC_QBUF Failed");
       return -1;
    }

     nQueued++;

    return ret;

}

status_t V4LCameraAdapter::setParameters(const CameraParameters &params)
{
    LOG_FUNCTION_NAME;

    status_t ret = NO_ERROR;

    // Udpate the current parameter set
    mParams = params;

    LOG_FUNCTION_NAME_EXIT;
    return ret;
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
            break;

        case CAMERA_IMAGE_CAPTURE:
            ret = UseBuffersCapture(bufArr, num);
            break;

        case CAMERA_VIDEO:
            //@warn Video capture is not fully supported yet
            ret = UseBuffersPreview(bufArr, num);
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

status_t V4LCameraAdapter::UseBuffersPreview(void* bufArr, int num)
{
    int ret = NO_ERROR;

    if(NULL == bufArr)
        {
        return BAD_VALUE;
        }

    int width, height;
    mParams.getPreviewSize(&width, &height);
    setBuffersFormat(width, height, DEFAULT_PREVIEW_PIXEL_FORMAT);

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

    // Update the preview buffer count
    mPreviewBufferCount = num;

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

    LOGD("UseBuffersCapture setBuffersFormat..");
    int width, height;
    mParams.getPictureSize(&width, &height);
    setBuffersFormat(width, height, DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT);

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

  Mutex::Autolock lock(mPreviewBufsLock);

  if(mPreviewing)
    {
    return BAD_VALUE;
    }

   for (int i = 0; i < mPreviewBufferCount; i++) {

       mVideoInfo->buf.index = i;
       mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
       mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

       ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
       if (ret < 0) {
           CAMHAL_LOGEA("VIDIOC_QBUF Failed");
           return -EINVAL;
       }
       LOGD("startPreview .length=%d", mVideoInfo->buf.length);

       nQueued++;
   }

    enum v4l2_buf_type bufType;
   if (!mVideoInfo->isStreaming) {
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

    return ret;

}

char * V4LCameraAdapter::GetFrame(int &index)
{
    int ret;

    mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

    /* DQ */
    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &mVideoInfo->buf);
    if (ret < 0) {
        CAMHAL_LOGEA("GetFrame: VIDIOC_DQBUF Failed");
        return NULL;
    }
    nDequeued++;

    index = mVideoInfo->buf.index;

    return (char *)mVideoInfo->mem[mVideoInfo->buf.index];
}

//API to get the frame size required to be allocated. This size is used to override the size passed
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
    length = width * height * 3; //rgb24
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

    // Nothing useful to do in the constructor

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
            return BAD_VALUE;
            }

        uint8_t* ptr = (uint8_t*) mPreviewBufs.keyAt(index);
        private_handle_t* gralloc_hnd = (private_handle_t*)ptr;
        if (!ptr)
            {
            return BAD_VALUE;
            }

        int width, height;
        uint8_t* dest = (uint8_t*)gralloc_hnd->base; //ptr;
        uint8_t* src = (uint8_t*) fp;
        mParams.getPreviewSize(&width, &height);
        memcpy(dest,src,width*height*3/2);

        mParams.getPreviewSize(&width, &height);
        frame.mFrameMask = CameraFrame::PREVIEW_FRAME_SYNC;
        frame.mFrameType = CameraFrame::PREVIEW_FRAME_SYNC;
        frame.mBuffer = ptr; //dest
        frame.mLength = width*height*2;
        frame.mAlignment = width*2;
        frame.mOffset = 0;
        frame.mYuv[0] = NULL;
        frame.mYuv[1] = NULL;
        frame.mWidth = width;
        frame.mHeight = height;
        frame.mTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);;
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

    if (true)
        {
        mVideoInfo->buf.index = 0;
        mVideoInfo->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
        if (ret < 0) {
            CAMHAL_LOGEA("VIDIOC_QBUF Failed");
            return -EINVAL;
        }

        enum v4l2_buf_type bufType;
        if (!mVideoInfo->isStreaming) {
            bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

            ret = ioctl (mCameraHandle, VIDIOC_STREAMON, &bufType);
            if (ret < 0) {
                CAMHAL_LOGEB("StartStreaming: Unable to start capture: %s", strerror(errno));
                return ret;
            }

            mVideoInfo->isStreaming = true;
        }

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
        uint16_t* dest = (uint16_t*)mCaptureBuf->data;
        uint16_t* src = (uint16_t*) fp;
        mParams.getPictureSize(&width, &height);
        LOGD("pictureThread mCaptureBuf=%#x dest=%#x fp=%#x width=%d height=%d", mCaptureBuf, dest, fp, width, height);
        LOGD("length=%d bytesused=%d index=%d", mVideoInfo->buf.length, mVideoInfo->buf.bytesused, index);

        memcpy(dest, src, mVideoInfo->buf.length);

        notifyShutterSubscribers();
        //TODO correct time to call this?
        if (NULL != mEndImageCaptureCallback)
            mEndImageCaptureCallback(mEndCaptureData);

        frame.mFrameMask = CameraFrame::IMAGE_FRAME;
        frame.mFrameType = CameraFrame::IMAGE_FRAME;
        frame.mQuirks = CameraFrame::ENCODE_RAW_RGB24_TO_JPEG;
        frame.mBuffer = mCaptureBuf->data;
        frame.mLength = width*height*2;
        frame.mAlignment = width*2;
        frame.mOffset = 0;
        frame.mYuv[0] = NULL;
        frame.mYuv[1] = NULL;
        frame.mWidth = width;
        frame.mHeight = height;
        frame.mTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);;

        
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

        /* Unmap buffers */
        if (munmap(mVideoInfo->mem[0], mVideoInfo->buf.length) < 0)
            CAMHAL_LOGEA("Unmap failed");

        }

    // start preview thread again after stopping it in UseBuffersCapture
    {
        Mutex::Autolock lock(mPreviewBufferLock);
        UseBuffersPreview(mPreviewBuffers, mPreviewBufferCount);
        startPreview();
    }


    ret = setInitFrameRefCount(frame.mBuffer, frame.mFrameMask);
    if (ret)
        LOGE("setInitFrameRefCount err=%d", ret);
    else
        ret = sendFrameToSubscribers(&frame);
    //LOGD("pictureThread /sendFrameToSubscribers ret=%d", ret);

    return ret;
}


// ---------------------------------------------------------------------------
extern "C" CameraAdapter* CameraAdapter_Factory()
{
    CameraAdapter *adapter = NULL;
    Mutex::Autolock lock(gAdapterLock);

    LOG_FUNCTION_NAME;

    adapter = new V4LCameraAdapter(0/*sensor_index*/);
    if ( adapter ) {
        CAMHAL_LOGDB("New V4L Camera adapter instance created for sensor %d",0/*sensor_index*/);
    } else {
        CAMHAL_LOGEA("Camera adapter create failed!");
    }

    LOG_FUNCTION_NAME_EXIT;

    return adapter;
}

extern "C" int CameraAdapter_Capabilities(CameraProperties::Properties* properties_array,
                                          const unsigned int starting_camera,
                                          const unsigned int max_camera) {
    int num_cameras_supported = 0;
    CameraProperties::Properties* properties = NULL;

    LOG_FUNCTION_NAME;

    if(!properties_array)
    {
        return -EINVAL;
    }

    // TODO: Need to tell camera properties what other cameras we can support
    if (starting_camera + num_cameras_supported < max_camera) {
        num_cameras_supported++;
        properties = properties_array + starting_camera;
        properties->set(CameraProperties::CAMERA_NAME, "Camera");
        //TODO move
        extern void loadCaps(int camera_id, CameraProperties::Properties* params);
        loadCaps(0, properties);
    }


    //------------------------

    LOG_FUNCTION_NAME_EXIT;

    return num_cameras_supported;
}

extern "C" int getValidFrameSize(int camera_id, int pixel_format, char *framesize)
{
    struct v4l2_frmsizeenum frmsize;
    int fd, i=0;
    char tempsize[12];
    framesize[0] = '\0';
    fd = open(device, O_RDWR);
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

//TODO move
extern "C" void loadCaps(int camera_id, CameraProperties::Properties* params) {
    const char DEFAULT_ANTIBANDING[] = "auto";
    const char DEFAULT_BRIGHTNESS[] = "50";
    const char DEFAULT_CONTRAST[] = "100";
    const char DEFAULT_EFFECT[] = "none";
    const char DEFAULT_EV_COMPENSATION[] = "0";
    const char DEFAULT_EV_STEP[] = "0.1";
    const char DEFAULT_EXPOSURE_MODE[] = "auto";
    const char DEFAULT_FLASH_MODE[] = "off";
    const char DEFAULT_FOCUS_MODE_PREFERRED[] = "auto";
    const char DEFAULT_FOCUS_MODE[] = "infinity";
    const char DEFAULT_FRAMERATE_RANGE_IMAGE[] = "15000,20000";
    const char DEFAULT_FRAMERATE_RANGE_VIDEO[]="15000,20000";
    const char DEFAULT_IPP[] = "ldc-nsf";
    const char DEFAULT_GBCE[] = "disable";
    const char DEFAULT_ISO_MODE[] = "auto";
    const char DEFAULT_JPEG_QUALITY[] = "90";
    const char DEFAULT_THUMBNAIL_QUALITY[] = "90";
    const char DEFAULT_THUMBNAIL_SIZE[] = "160x120";
    const char DEFAULT_PICTURE_FORMAT[] = "jpeg";
    const char DEFAULT_PICTURE_SIZE[] = "640x480";
    const char DEFAULT_PREVIEW_FORMAT[] = "yuv420sp";
    const char DEFAULT_FRAMERATE[] = "15";
    const char DEFAULT_PREVIEW_SIZE[] = "640x480";
    const char DEFAULT_NUM_PREV_BUFS[] = "6";
    const char DEFAULT_NUM_PIC_BUFS[] = "1";
    const char DEFAULT_MAX_FOCUS_AREAS[] = "1";
    const char DEFAULT_SATURATION[] = "100";
    const char DEFAULT_SCENE_MODE[] = "auto";
    const char DEFAULT_SHARPNESS[] = "100";
    const char DEFAULT_VSTAB[] = "false";
    const char DEFAULT_VSTAB_SUPPORTED[] = "true";
    const char DEFAULT_WB[] = "auto";
    const char DEFAULT_ZOOM[] = "0";
    const char DEFAULT_MAX_FD_HW_FACES[] = "0";
    const char DEFAULT_MAX_FD_SW_FACES[] = "0";
    const char DEFAULT_FOCAL_LENGTH_PRIMARY[] = "3.43";
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

    params->set(CameraProperties::FACING_INDEX, TICameraParameters::FACING_FRONT);
    params->set(CameraProperties::ANTIBANDING, DEFAULT_ANTIBANDING);
    params->set(CameraProperties::BRIGHTNESS, DEFAULT_BRIGHTNESS);
    params->set(CameraProperties::CONTRAST, DEFAULT_CONTRAST);
    params->set(CameraProperties::EFFECT, DEFAULT_EFFECT);
    params->set(CameraProperties::EV_COMPENSATION, DEFAULT_EV_COMPENSATION);
    params->set(CameraProperties::SUPPORTED_EV_STEP, DEFAULT_EV_STEP);
    params->set(CameraProperties::EXPOSURE_MODE, DEFAULT_EXPOSURE_MODE);
    params->set(CameraProperties::FLASH_MODE, DEFAULT_FLASH_MODE);
    char *pos = strstr(params->get(CameraProperties::SUPPORTED_FOCUS_MODES), DEFAULT_FOCUS_MODE_PREFERRED);
    if ( NULL != pos )
        {
        params->set(CameraProperties::FOCUS_MODE, DEFAULT_FOCUS_MODE_PREFERRED);
        }
    else
        {
        params->set(CameraProperties::FOCUS_MODE, DEFAULT_FOCUS_MODE);
        }
    params->set(CameraProperties::IPP, DEFAULT_IPP);
    params->set(CameraProperties::GBCE, DEFAULT_GBCE);
    params->set(CameraProperties::ISO_MODE, DEFAULT_ISO_MODE);
    params->set(CameraProperties::JPEG_QUALITY, DEFAULT_JPEG_QUALITY);
    params->set(CameraProperties::JPEG_THUMBNAIL_QUALITY, DEFAULT_THUMBNAIL_QUALITY);
    params->set(CameraProperties::JPEG_THUMBNAIL_SIZE, DEFAULT_THUMBNAIL_SIZE);
    params->set(CameraProperties::PICTURE_FORMAT, DEFAULT_PICTURE_FORMAT);
    params->set(CameraProperties::PREVIEW_FORMAT, DEFAULT_PREVIEW_FORMAT);
    params->set(CameraProperties::PREVIEW_FRAME_RATE, DEFAULT_FRAMERATE);
    params->set(CameraProperties::FRAMERATE_RANGE_IMAGE, DEFAULT_FRAMERATE_RANGE_IMAGE);
    params->set(CameraProperties::FRAMERATE_RANGE_VIDEO, DEFAULT_FRAMERATE_RANGE_VIDEO);
    params->set(CameraProperties::REQUIRED_PREVIEW_BUFS, DEFAULT_NUM_PREV_BUFS);
    params->set(CameraProperties::REQUIRED_IMAGE_BUFS, DEFAULT_NUM_PIC_BUFS);
    params->set(CameraProperties::SATURATION, DEFAULT_SATURATION);
    params->set(CameraProperties::SCENE_MODE, DEFAULT_SCENE_MODE);
    params->set(CameraProperties::SHARPNESS, DEFAULT_SHARPNESS);
    params->set(CameraProperties::VSTAB, DEFAULT_VSTAB);
    params->set(CameraProperties::VSTAB_SUPPORTED, DEFAULT_VSTAB_SUPPORTED);
    params->set(CameraProperties::WHITEBALANCE, DEFAULT_WB);
    params->set(CameraProperties::ZOOM, DEFAULT_ZOOM);
    params->set(CameraProperties::MAX_FD_HW_FACES, DEFAULT_MAX_FD_HW_FACES);
    params->set(CameraProperties::MAX_FD_SW_FACES, DEFAULT_MAX_FD_SW_FACES);
    params->set(CameraProperties::AUTO_EXPOSURE_LOCK, DEFAULT_AE_LOCK);
    params->set(CameraProperties::AUTO_WHITEBALANCE_LOCK, DEFAULT_AWB_LOCK);
    params->set(CameraProperties::FOCAL_LENGTH, DEFAULT_FOCAL_LENGTH_PRIMARY);
    params->set(CameraProperties::HOR_ANGLE, DEFAULT_HOR_ANGLE);
    params->set(CameraProperties::VER_ANGLE, DEFAULT_VER_ANGLE);
    params->set(CameraProperties::VIDEO_SNAPSHOT_SUPPORTED, DEFAULT_VIDEO_SNAPSHOT_SUPPORTED);
    params->set(CameraProperties::VIDEO_SIZE, DEFAULT_VIDEO_SIZE);
    params->set(CameraProperties::PREFERRED_PREVIEW_SIZE_FOR_VIDEO, DEFAULT_PREFERRED_PREVIEW_SIZE_FOR_VIDEO);

    params->set(CameraProperties::FRAMERATE_RANGE, "10500,26623");

    char sizes[64];
    if (!getValidFrameSize(camera_id, DEFAULT_PREVIEW_PIXEL_FORMAT, sizes)) {
        int len = strlen(sizes);
        if(len>1){
            if(sizes[len-1] == ',')
                sizes[len-1] = '\0';
        }
        params->set(CameraProperties::SUPPORTED_PREVIEW_SIZES, sizes);
        //set last size as default
        char * b = strrchr(sizes, ',');
        if (b) b++;
        else b = sizes;
        params->set(CameraProperties::PREVIEW_SIZE, b);
    } else
        params->set(CameraProperties::SUPPORTED_PREVIEW_SIZES, "640x480");

    if (!getValidFrameSize(camera_id, DEFAULT_IMAGE_CAPTURE_PIXEL_FORMAT, sizes)) {
        int len = strlen(sizes);
        if(len>1){
            if(sizes[len-1] == ',')
                sizes[len-1] = '\0';
        }
        params->set(CameraProperties::SUPPORTED_PICTURE_SIZES, sizes);
        //set last size as default
        char * b = strrchr(sizes, ',');
        if (b) b++;
        else b = sizes;
        params->set(CameraProperties::PICTURE_SIZE, b);
    } else
        params->set(CameraProperties::SUPPORTED_PICTURE_SIZES, "640x480");

    params->set(CameraProperties::SUPPORTED_THUMBNAIL_SIZES, "180x160,0x0");

    params->set(CameraProperties::SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV420SP);
    params->set(CameraProperties::SUPPORTED_IPP_MODES, DEFAULT_IPP);
    params->set(CameraProperties::SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
    params->set(CameraProperties::FRAMERATE_RANGE_SUPPORTED, "(10500,26623)");
    params->set(CameraProperties::SUPPORTED_PREVIEW_FRAME_RATES, "15,20");
    //params->set(CameraProperties::SUPPORTED_FOCUS_MODES, CameraParameters::FOCUS_MODE_CONTINUOUS_PICTURE);
    params->set(CameraProperties::SUPPORTED_FOCUS_MODES, DEFAULT_FOCUS_MODE);

    params->set(CameraProperties::SUPPORTED_EXPOSURE_MODES, "auto");
    params->set(CameraProperties::SUPPORTED_WHITE_BALANCE, "auto");
    params->set(CameraProperties::SUPPORTED_ANTIBANDING, "auto");
    params->set(CameraProperties::SUPPORTED_ISO_VALUES, "auto");
    params->set(CameraProperties::SUPPORTED_SCENE_MODES, "auto");
    params->set(CameraProperties::SUPPORTED_FLASH_MODES, "off");
    params->set(CameraProperties::SUPPORTED_EFFECTS, "none");
    params->set(CameraProperties::SUPPORTED_VIDEO_SIZES, "352x288,640x480");

}

};


/*--------------------Camera Adapter Class ENDS here-----------------------------*/

