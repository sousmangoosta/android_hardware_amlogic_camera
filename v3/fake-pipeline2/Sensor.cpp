/*
 * Copyright (C) 2012 The Android Open Source Project
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

//#define LOG_NDEBUG 0
//#define LOG_NNDEBUG 0
#define LOG_TAG "EmulatedCamera2_Sensor"

#ifdef LOG_NNDEBUG
#define ALOGVV(...) ALOGV(__VA_ARGS__)
#else
#define ALOGVV(...) ((void)0)
#endif

#include <utils/Log.h>
#include <cutils/properties.h>

#include "../EmulatedFakeCamera2.h"
#include "Sensor.h"
#include <cmath>
#include <cstdlib>
#include "system/camera_metadata.h"
#include "libyuv.h"

#define ARRAY_SIZE(x) (sizeof((x))/sizeof(((x)[0])))

namespace android {

const unsigned int Sensor::kResolution[2]  = {1600, 1200};

const nsecs_t Sensor::kExposureTimeRange[2] =
    {1000L, 30000000000L} ; // 1 us - 30 sec
const nsecs_t Sensor::kFrameDurationRange[2] =
    {33331760L, 30000000000L}; // ~1/30 s - 30 sec
const nsecs_t Sensor::kMinVerticalBlank = 10000L;

const uint8_t Sensor::kColorFilterArrangement =
    ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_RGGB;

// Output image data characteristics
const uint32_t Sensor::kMaxRawValue = 4000;
const uint32_t Sensor::kBlackLevel  = 1000;

// Sensor sensitivity
const float Sensor::kSaturationVoltage      = 0.520f;
const uint32_t Sensor::kSaturationElectrons = 2000;
const float Sensor::kVoltsPerLuxSecond      = 0.100f;

const float Sensor::kElectronsPerLuxSecond =
        Sensor::kSaturationElectrons / Sensor::kSaturationVoltage
        * Sensor::kVoltsPerLuxSecond;

const float Sensor::kBaseGainFactor = (float)Sensor::kMaxRawValue /
            Sensor::kSaturationElectrons;

const float Sensor::kReadNoiseStddevBeforeGain = 1.177; // in electrons
const float Sensor::kReadNoiseStddevAfterGain =  2.100; // in digital counts
const float Sensor::kReadNoiseVarBeforeGain =
            Sensor::kReadNoiseStddevBeforeGain *
            Sensor::kReadNoiseStddevBeforeGain;
const float Sensor::kReadNoiseVarAfterGain =
            Sensor::kReadNoiseStddevAfterGain *
            Sensor::kReadNoiseStddevAfterGain;

// While each row has to read out, reset, and then expose, the (reset +
// expose) sequence can be overlapped by other row readouts, so the final
// minimum frame duration is purely a function of row readout time, at least
// if there's a reasonable number of rows.
const nsecs_t Sensor::kRowReadoutTime =
            Sensor::kFrameDurationRange[0] / Sensor::kResolution[1];

const int32_t Sensor::kSensitivityRange[2] = {100, 800};
const uint32_t Sensor::kDefaultSensitivity = 100;

/** A few utility functions for math, normal distributions */

// Take advantage of IEEE floating-point format to calculate an approximate
// square root. Accurate to within +-3.6%
float sqrtf_approx(float r) {
    // Modifier is based on IEEE floating-point representation; the
    // manipulations boil down to finding approximate log2, dividing by two, and
    // then inverting the log2. A bias is added to make the relative error
    // symmetric about the real answer.
    const int32_t modifier = 0x1FBB4000;

    int32_t r_i = *(int32_t*)(&r);
    r_i = (r_i >> 1) + modifier;

    return *(float*)(&r_i);
}

void rgb24_memcpy(unsigned char *dst, unsigned char *src, int width, int height)
{
        int stride = (width + 31) & ( ~31);
        int w, h;
        for (h=0; h<height; h++)
        {
                memcpy( dst, src, width*3);
                dst += width*3;
                src += stride*3;
        }
}

static int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1);
}

Sensor::Sensor():
        Thread(false),
        mGotVSync(false),
        mExposureTime(kFrameDurationRange[0]-kMinVerticalBlank),
        mFrameDuration(kFrameDurationRange[0]),
        mGainFactor(kDefaultSensitivity),
        mNextBuffers(NULL),
        mFrameNumber(0),
        mCapturedBuffers(NULL),
        mListener(NULL),
        mScene(kResolution[0], kResolution[1], kElectronsPerLuxSecond)
{

}

Sensor::~Sensor() {
    shutDown();
}

status_t Sensor::startUp(int idx) {
    ALOGV("%s: E", __FUNCTION__);
    DBG_LOGA("ddd");

    int res;
    mCapturedBuffers = NULL;
    res = run("EmulatedFakeCamera2::Sensor",
            ANDROID_PRIORITY_URGENT_DISPLAY);

    if (res != OK) {
        ALOGE("Unable to start up sensor capture thread: %d", res);
    }

    vinfo = (struct VideoInfo *) calloc(1, sizeof(*vinfo));
    vinfo->idx = idx;

    res = camera_open(vinfo);
    if (res < 0) {
            ALOGE("Unable to open sensor %d, errno=%d\n", vinfo->idx, res);
    }

    return res;
}

status_t Sensor::setOutputFormat(int width, int height, int pixelformat)
{
    int res;

    if (pixelformat == V4L2_PIX_FMT_RGB24) {
        vinfo->picture.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vinfo->picture.format.fmt.pix.width = width;
        vinfo->picture.format.fmt.pix.height = height;
        vinfo->picture.format.fmt.pix.pixelformat = pixelformat;
    } else {
        vinfo->preview.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vinfo->preview.format.fmt.pix.width = width;
        vinfo->preview.format.fmt.pix.height = height;
        vinfo->preview.format.fmt.pix.pixelformat = pixelformat;

        res = setBuffersFormat(vinfo);
        if (res < 0) {
            ALOGE("set buffer failed\n");
            return res;
        }
    }

    return OK;

}

status_t Sensor::streamOn() {

    return start_capturing(vinfo);
}

bool Sensor::isStreaming() {

    return vinfo->isStreaming;
}

bool Sensor::isNeedRestart(int width, int height, int pixelformat)
{
    if ((vinfo->preview.format.fmt.pix.width != width)
        ||(vinfo->preview.format.fmt.pix.height != height)
        //||(vinfo->format.fmt.pix.pixelformat != pixelformat)
        ) {

        return true;

    }

    return false;
}
status_t Sensor::streamOff() {

    return stop_capturing(vinfo);
}

int Sensor::getOutputFormat(int pixelfmt)
{
    struct v4l2_fmtdesc fmt;
    int ret;
    memset(&fmt,0,sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (pixelfmt == HAL_PIXEL_FORMAT_YV12) {
		pixelfmt = V4L2_PIX_FMT_YVU420;
	} else if (pixelfmt == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
		pixelfmt = V4L2_PIX_FMT_NV21;
	} else if (pixelfmt == HAL_PIXEL_FORMAT_YCbCr_422_I) {
		pixelfmt = V4L2_PIX_FMT_YUYV;
	} else {
		pixelfmt = V4L2_PIX_FMT_NV21;
	}

	fmt.index = 0;
    while ((ret = ioctl(vinfo->fd, VIDIOC_ENUM_FMT, &fmt)) == 0){
        if (fmt.pixelformat == V4L2_PIX_FMT_MJPEG)
            return V4L2_PIX_FMT_MJPEG;
        fmt.index++;
    }
	
    fmt.index = 0;
    while ((ret = ioctl(vinfo->fd, VIDIOC_ENUM_FMT, &fmt)) == 0){
        if (fmt.pixelformat == pixelfmt)
            return pixelfmt;
        fmt.index++;
    }
	
    ALOGE("Unable to find a supported sensor format!");
    return BAD_VALUE;
}

void Sensor::setPictureRotate(int rotate)
{
	mRotateValue = rotate;
}
int Sensor::getPictureRotate()
{
	return mRotateValue;
}
status_t Sensor::shutDown() {
    ALOGV("%s: E", __FUNCTION__);

    int res;
    res = requestExitAndWait();
    if (res != OK) {
        ALOGE("Unable to shut down sensor capture thread: %d", res);
    }

    if (vinfo != NULL) {
        stop_capturing(vinfo);
    }

    camera_close(vinfo);

    if (vinfo){
            free(vinfo);
            vinfo = NULL;
    }
    ALOGD("%s: Exit", __FUNCTION__);
    return res;
}

Scene &Sensor::getScene() {
    return mScene;
}

int Sensor::getZoom(int *zoomMin, int *zoomMax, int *zoomStep)
{
    int ret = 0;
    struct v4l2_queryctrl qc;

    memset(&qc, 0, sizeof(qc));
    qc.id = V4L2_CID_ZOOM_ABSOLUTE;
    ret = ioctl (vinfo->fd, VIDIOC_QUERYCTRL, &qc);

    if ((qc.flags == V4L2_CTRL_FLAG_DISABLED) || ( ret < 0)
                   || (qc.type != V4L2_CTRL_TYPE_INTEGER)) {
        ret = -1;
        ALOGE("%s: Can't get zoom level!\n", __FUNCTION__);
    } else {
        *zoomMin = qc.minimum;
        *zoomMax = qc.maximum;
        *zoomStep = qc.step;
        DBG_LOGB("zoomMin:%dzoomMax:%dzoomStep:%d\n", *zoomMin, *zoomMax, *zoomStep);
    }

    return ret ;
}

int Sensor::setZoom(int zoomValue)
{
    int ret = 0;
    struct v4l2_control ctl;

    memset( &ctl, 0, sizeof(ctl));
    ctl.value = zoomValue;
    ctl.id = V4L2_CID_ZOOM_ABSOLUTE;
    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    if (ret < 0) {
        ALOGE("%s: Set zoom level failed!\n", __FUNCTION__);
    }
    return ret ;
}

status_t Sensor::setEffect(uint8_t effect)
{
    int ret = 0;
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_COLORFX;

    switch (effect) {
    case ANDROID_CONTROL_EFFECT_MODE_OFF:
        ctl.value= CAM_EFFECT_ENC_NORMAL;
        break;
    case ANDROID_CONTROL_EFFECT_MODE_NEGATIVE:
        ctl.value= CAM_EFFECT_ENC_COLORINV;
        break;
    case ANDROID_CONTROL_EFFECT_MODE_SEPIA:
        ctl.value= CAM_EFFECT_ENC_SEPIA;
        break;
    default:
        ALOGE("%s: Doesn't support effect mode %d",
                    __FUNCTION__, effect);
        return BAD_VALUE;
    }

    DBG_LOGB("set effect mode:%d", effect);
    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    if (ret < 0) {
        CAMHAL_LOGDB("Set effect fail: %s. ret=%d", strerror(errno),ret);
    }
    return ret ;
}

#define MAX_LEVEL_FOR_EXPOSURE 16
#define MIN_LEVEL_FOR_EXPOSURE 3

int Sensor::getExposure(int *maxExp, int *minExp, int *def, camera_metadata_rational *step)
{
    struct v4l2_queryctrl qc;
    int ret=0;
    int level = 0;
    int middle = 0;

    memset( &qc, 0, sizeof(qc));

        DBG_LOGA("getExposure\n");
    qc.id = V4L2_CID_EXPOSURE;
    ret = ioctl(vinfo->fd, VIDIOC_QUERYCTRL, &qc);
    if(ret < 0) {
        CAMHAL_LOGDB("QUERYCTRL failed, errno=%d\n", errno);
        *minExp = -4;
        *maxExp = 4;
        *def = 0;
        step->numerator = 1;
        step->denominator = 1;
        return ret;
    }

    if(0 < qc.step)
        level = ( qc.maximum - qc.minimum + 1 )/qc.step;

    if((level > MAX_LEVEL_FOR_EXPOSURE)
      || (level < MIN_LEVEL_FOR_EXPOSURE)){
        *minExp = -4;
        *maxExp = 4;
        *def = 0;
        step->numerator = 1;
        step->denominator = 1;
        DBG_LOGB("not in[min,max], min=%d, max=%d, def=%d\n",
                                        *minExp, *maxExp, *def);
        return true;
    }

    middle = (qc.minimum+qc.maximum)/2;
    *minExp = qc.minimum - middle;
    *maxExp = qc.maximum - middle;
    *def = qc.default_value - middle;
    step->numerator = 1;
    step->denominator = qc.step;
        DBG_LOGB("min=%d, max=%d, step=%d\n", qc.minimum, qc.maximum, qc.step);
    return ret;
}

status_t Sensor::setExposure(int expCmp)
{
    int ret = 0;
    struct v4l2_control ctl;
    struct v4l2_queryctrl qc;

    if(mEV == expCmp){
        return 0;
    }else{
        mEV = expCmp;
    }
    memset(&ctl, 0, sizeof(ctl));
    memset(&qc, 0, sizeof(qc));

    qc.id = V4L2_CID_EXPOSURE;

    ret = ioctl(vinfo->fd, VIDIOC_QUERYCTRL, &qc);
    if (ret < 0) {
        CAMHAL_LOGDB("AMLOGIC CAMERA get Exposure fail: %s. ret=%d", strerror(errno),ret);
    }

    ctl.id = V4L2_CID_EXPOSURE;
    ctl.value = expCmp + (qc.maximum - qc.minimum) / 2;

    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    if (ret < 0) {
        CAMHAL_LOGDB("AMLOGIC CAMERA Set Exposure fail: %s. ret=%d", strerror(errno),ret);
    }
        DBG_LOGB("setExposure value%d mEVmin%d mEVmax%d\n",ctl.value, qc.minimum, qc.maximum);
    return ret ;
}

int Sensor::getAntiBanding(uint8_t *antiBanding, uint8_t maxCont)
{
    struct v4l2_queryctrl qc;
    struct v4l2_querymenu qm;
    int ret;
    int mode_count = -1;

    memset(&qc, 0, sizeof(struct v4l2_queryctrl));
    qc.id = V4L2_CID_POWER_LINE_FREQUENCY;
    ret = ioctl (vinfo->fd, VIDIOC_QUERYCTRL, &qc);
    if ( (ret<0) || (qc.flags == V4L2_CTRL_FLAG_DISABLED)){
        DBG_LOGB("camera handle %d can't support this ctrl",vinfo->fd);
    } else if ( qc.type != V4L2_CTRL_TYPE_INTEGER) {
        DBG_LOGB("this ctrl of camera handle %d can't support menu type",vinfo->fd);
    } else {
        memset(&qm, 0, sizeof(qm));

        int index = 0;
        mode_count = 1;
        antiBanding[0] = ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF;

        for (index = qc.minimum; index <= qc.maximum; index+= qc.step) {
            if (mode_count >= maxCont)
                break;

            memset(&qm, 0, sizeof(struct v4l2_querymenu));
            qm.id = V4L2_CID_POWER_LINE_FREQUENCY;
            qm.index = index;
            if(ioctl (vinfo->fd, VIDIOC_QUERYMENU, &qm) < 0){
                continue;
            } else {
                if (strcmp((char*)qm.name,"50hz") == 0) {
                    antiBanding[mode_count] = ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"60hz") == 0) {
                    antiBanding[mode_count] = ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"auto") == 0) {
                    antiBanding[mode_count] = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
                    mode_count++;
                }

            }
        }
    }

    return mode_count;
}

status_t Sensor::setAntiBanding(uint8_t antiBanding)
{
    int ret = 0;
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_POWER_LINE_FREQUENCY;

    switch (antiBanding) {
    case ANDROID_CONTROL_AE_ANTIBANDING_MODE_OFF:
        ctl.value= CAM_ANTIBANDING_OFF;
        break;
    case ANDROID_CONTROL_AE_ANTIBANDING_MODE_50HZ:
        ctl.value= CAM_ANTIBANDING_50HZ;
        break;
    case ANDROID_CONTROL_AE_ANTIBANDING_MODE_60HZ:
        ctl.value= CAM_ANTIBANDING_60HZ;
        break;
    case ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO:
        ctl.value= CAM_ANTIBANDING_AUTO;
        break;
    default:
            ALOGE("%s: Doesn't support ANTIBANDING mode %d",
                    __FUNCTION__, antiBanding);
            return BAD_VALUE;
    }

    DBG_LOGB("anti banding mode:%d", antiBanding);
    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    if ( ret < 0) {
        CAMHAL_LOGDA("failed to set anti banding mode!\n");
        return BAD_VALUE;
    }
    return ret;
}

status_t Sensor::setFocuasArea(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int ret = 0;
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_FOCUS_ABSOLUTE;
    ctl.value = ((x0 + x1) / 2 + 1000) << 16;
    ctl.value |= ((y0 + y1) / 2 + 1000) & 0xffff;

    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    return ret;
}


int Sensor::getAutoFocus(uint8_t *afMode, uint8_t maxCount)
{
    struct v4l2_queryctrl qc;
    struct v4l2_querymenu qm;
    int ret;
    int mode_count = -1;

    memset(&qc, 0, sizeof(struct v4l2_queryctrl));
    qc.id = V4L2_CID_FOCUS_AUTO;
    ret = ioctl (vinfo->fd, VIDIOC_QUERYCTRL, &qc);
    if( (ret<0) || (qc.flags == V4L2_CTRL_FLAG_DISABLED)){
        DBG_LOGB("camera handle %d can't support this ctrl",vinfo->fd);
    }else if( qc.type != V4L2_CTRL_TYPE_MENU) {
        DBG_LOGB("this ctrl of camera handle %d can't support menu type",vinfo->fd);
    }else{
        memset(&qm, 0, sizeof(qm));

        int index = 0;
        mode_count = 1;
        afMode[0] = ANDROID_CONTROL_AF_MODE_OFF;

        for (index = qc.minimum; index <= qc.maximum; index+= qc.step) {
            if (mode_count >= maxCount)
                break;

            memset(&qm, 0, sizeof(struct v4l2_querymenu));
            qm.id = V4L2_CID_FOCUS_AUTO;
            qm.index = index;
            if(ioctl (vinfo->fd, VIDIOC_QUERYMENU, &qm) < 0){
                continue;
            } else {
                if (strcmp((char*)qm.name,"auto") == 0) {
                    afMode[mode_count] = ANDROID_CONTROL_AF_MODE_AUTO;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"continuous-video") == 0) {
                    afMode[mode_count] = ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"continuous-picture") == 0) {
                    afMode[mode_count] = ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE;
                    mode_count++;
                }

            }
        }
    }

    return mode_count;
}

status_t Sensor::setAutoFocuas(uint8_t afMode)
{
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_FOCUS_AUTO;

    switch (afMode) {
        case ANDROID_CONTROL_AF_MODE_AUTO:
            ctl.value = CAM_FOCUS_MODE_AUTO;
            break;
        case ANDROID_CONTROL_AF_MODE_MACRO:
            ctl.value = CAM_FOCUS_MODE_MACRO;
            break;
        case ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO:
            ctl.value = CAM_FOCUS_MODE_CONTI_VID;
            break;
        case ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE:
            ctl.value = CAM_FOCUS_MODE_CONTI_PIC;
            break;
        default:
            ALOGE("%s: Emulator doesn't support AF mode %d",
                    __FUNCTION__, afMode);
            return BAD_VALUE;
    }

    if (ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl) < 0) {
        CAMHAL_LOGDA("failed to set camera focuas mode!\n");
        return BAD_VALUE;
    }

    return OK;
}

int Sensor::getAWB(uint8_t *awbMode, uint8_t maxCount)
{
    struct v4l2_queryctrl qc;
    struct v4l2_querymenu qm;
    int ret;
    int mode_count = -1;

    memset(&qc, 0, sizeof(struct v4l2_queryctrl));
    qc.id = V4L2_CID_DO_WHITE_BALANCE;
    ret = ioctl (vinfo->fd, VIDIOC_QUERYCTRL, &qc);
    if( (ret<0) || (qc.flags == V4L2_CTRL_FLAG_DISABLED)){
        DBG_LOGB("camera handle %d can't support this ctrl",vinfo->fd);
    }else if( qc.type != V4L2_CTRL_TYPE_MENU) {
        DBG_LOGB("this ctrl of camera handle %d can't support menu type",vinfo->fd);
    }else{
        memset(&qm, 0, sizeof(qm));

        int index = 0;
        mode_count = 1;
        awbMode[0] = ANDROID_CONTROL_AWB_MODE_OFF;

        for (index = qc.minimum; index <= qc.maximum; index+= qc.step) {
            if (mode_count >= maxCount)
                break;

            memset(&qm, 0, sizeof(struct v4l2_querymenu));
            qm.id = V4L2_CID_DO_WHITE_BALANCE;
            qm.index = index;
            if(ioctl (vinfo->fd, VIDIOC_QUERYMENU, &qm) < 0){
                continue;
            } else {
                if (strcmp((char*)qm.name,"auto") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_AUTO;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"daylight") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_DAYLIGHT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"incandescent") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_INCANDESCENT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"fluorescent") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_FLUORESCENT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"warm-fluorescent") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_WARM_FLUORESCENT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"cloudy-daylight") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_CLOUDY_DAYLIGHT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"twilight") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_TWILIGHT;
                    mode_count++;
                } else if (strcmp((char*)qm.name,"shade") == 0) {
                    awbMode[mode_count] = ANDROID_CONTROL_AWB_MODE_SHADE;
                    mode_count++;
                }

            }
        }
    }

    return mode_count;
}

status_t Sensor::setAWB(uint8_t awbMode)
{
    int ret = 0;
    struct v4l2_control ctl;
    ctl.id = V4L2_CID_DO_WHITE_BALANCE;

    switch (awbMode) {
        case ANDROID_CONTROL_AWB_MODE_AUTO:
            ctl.value = CAM_WB_AUTO;
            break;
        case ANDROID_CONTROL_AWB_MODE_INCANDESCENT:
            ctl.value = CAM_WB_INCANDESCENCE;
            break;
        case ANDROID_CONTROL_AWB_MODE_FLUORESCENT:
            ctl.value = CAM_WB_FLUORESCENT;
            break;
        case ANDROID_CONTROL_AWB_MODE_DAYLIGHT:
            ctl.value = CAM_WB_DAYLIGHT;
            break;
        case ANDROID_CONTROL_AWB_MODE_SHADE:
            ctl.value = CAM_WB_SHADE;
            break;
        default:
            ALOGE("%s: Emulator doesn't support AWB mode %d",
                    __FUNCTION__, awbMode);
            return BAD_VALUE;
    }
    ret = ioctl(vinfo->fd, VIDIOC_S_CTRL, &ctl);
    return ret;
}

void Sensor::setExposureTime(uint64_t ns) {
    Mutex::Autolock lock(mControlMutex);
    ALOGVV("Exposure set to %f", ns/1000000.f);
    mExposureTime = ns;
}

void Sensor::setFrameDuration(uint64_t ns) {
    Mutex::Autolock lock(mControlMutex);
    ALOGVV("Frame duration set to %f", ns/1000000.f);
    mFrameDuration = ns;
}

void Sensor::setSensitivity(uint32_t gain) {
    Mutex::Autolock lock(mControlMutex);
    ALOGVV("Gain set to %d", gain);
    mGainFactor = gain;
}

void Sensor::setDestinationBuffers(Buffers *buffers) {
    Mutex::Autolock lock(mControlMutex);
    mNextBuffers = buffers;
}

void Sensor::setFrameNumber(uint32_t frameNumber) {
    Mutex::Autolock lock(mControlMutex);
    mFrameNumber = frameNumber;
}

bool Sensor::waitForVSync(nsecs_t reltime) {
    int res;
    Mutex::Autolock lock(mControlMutex);

    mGotVSync = false;
    res = mVSync.waitRelative(mControlMutex, reltime);
    if (res != OK && res != TIMED_OUT) {
        ALOGE("%s: Error waiting for VSync signal: %d", __FUNCTION__, res);
        return false;
    }
    return mGotVSync;
}

bool Sensor::waitForNewFrame(nsecs_t reltime,
        nsecs_t *captureTime) {
    Mutex::Autolock lock(mReadoutMutex);
    uint8_t *ret;
    if (mCapturedBuffers == NULL) {
        int res;
        res = mReadoutAvailable.waitRelative(mReadoutMutex, reltime);
        if (res == TIMED_OUT) {
            return false;
        } else if (res != OK || mCapturedBuffers == NULL) {
            ALOGE("Error waiting for sensor readout signal: %d", res);
            return false;
        }
    } else {
        mReadoutComplete.signal();
    }

    *captureTime = mCaptureTime;
    mCapturedBuffers = NULL;
    return true;
}

Sensor::SensorListener::~SensorListener() {
}

void Sensor::setSensorListener(SensorListener *listener) {
    Mutex::Autolock lock(mControlMutex);
    mListener = listener;
}

status_t Sensor::readyToRun() {
    int res;
    ALOGV("Starting up sensor thread");
    mStartupTime = systemTime();
    mNextCaptureTime = 0;
    mNextCapturedBuffers = NULL;

    DBG_LOGA("");

    return OK;
}

bool Sensor::threadLoop() {
    /**
     * Sensor capture operation main loop.
     *
     * Stages are out-of-order relative to a single frame's processing, but
     * in-order in time.
     */

    /**
     * Stage 1: Read in latest control parameters
     */
    uint64_t exposureDuration;
    uint64_t frameDuration;
    uint32_t gain;
    Buffers *nextBuffers;
    uint32_t frameNumber;
    SensorListener *listener = NULL;
    {
        Mutex::Autolock lock(mControlMutex);
        exposureDuration = mExposureTime;
        frameDuration    = mFrameDuration;
        gain             = mGainFactor;
        nextBuffers      = mNextBuffers;
        frameNumber      = mFrameNumber;
        listener         = mListener;
        // Don't reuse a buffer set
        mNextBuffers = NULL;

        // Signal VSync for start of readout
        ALOGVV("Sensor VSync");
        mGotVSync = true;
        mVSync.signal();
    }

    /**
     * Stage 3: Read out latest captured image
     */

    Buffers *capturedBuffers = NULL;
    nsecs_t captureTime = 0;

    nsecs_t startRealTime  = systemTime();
    // Stagefright cares about system time for timestamps, so base simulated
    // time on that.
    nsecs_t simulatedTime    = startRealTime;
    nsecs_t frameEndRealTime = startRealTime + frameDuration;
    nsecs_t frameReadoutEndRealTime = startRealTime +
            kRowReadoutTime * kResolution[1];

    if (mNextCapturedBuffers != NULL) {
        ALOGVV("Sensor starting readout");
        // Pretend we're doing readout now; will signal once enough time has elapsed
        capturedBuffers = mNextCapturedBuffers;
        captureTime    = mNextCaptureTime;
    }
    simulatedTime += kRowReadoutTime + kMinVerticalBlank;

    // TODO: Move this signal to another thread to simulate readout
    // time properly
    if (capturedBuffers != NULL) {
        ALOGVV("Sensor readout complete");
        Mutex::Autolock lock(mReadoutMutex);
        if (mCapturedBuffers != NULL) {
            ALOGV("Waiting for readout thread to catch up!");
            mReadoutComplete.wait(mReadoutMutex);
        }

        mCapturedBuffers = capturedBuffers;
        mCaptureTime = captureTime;
        mReadoutAvailable.signal();
        capturedBuffers = NULL;
    }

    /**
     * Stage 2: Capture new image
     */
    mNextCaptureTime = simulatedTime;
    mNextCapturedBuffers = nextBuffers;

    if (mNextCapturedBuffers != NULL) {
        mKernelBuffer = NULL;
        if (listener != NULL) {
            listener->onSensorEvent(frameNumber, SensorListener::EXPOSURE_START,
                    mNextCaptureTime);
        }
        ALOGVV("Starting next capture: Exposure: %f ms, gain: %d",
                (float)exposureDuration/1e6, gain);
        mScene.setExposureDuration((float)exposureDuration/1e9);
        mScene.calculateScene(mNextCaptureTime);

        // Might be adding more buffers, so size isn't constant
        for (size_t i = 0; i < mNextCapturedBuffers->size(); i++) {
            const StreamBuffer &b = (*mNextCapturedBuffers)[i];
            ALOGVV("Sensor capturing buffer %d: stream %d,"
                    " %d x %d, format %x, stride %d, buf %p, img %p",
                    i, b.streamId, b.width, b.height, b.format, b.stride,
                    b.buffer, b.img);
            switch(b.format) {
                case HAL_PIXEL_FORMAT_RAW_SENSOR:
                    captureRaw(b.img, gain, b.stride);
                    break;
                case HAL_PIXEL_FORMAT_RGB_888:
                    captureRGB(b.img, gain, b.stride);
                    break;
                case HAL_PIXEL_FORMAT_RGBA_8888:
                    captureRGBA(b.img, gain, b.stride);
                    break;
                case HAL_PIXEL_FORMAT_BLOB:
                    // Add auxillary buffer of the right size
                    // Assumes only one BLOB (JPEG) buffer in
                    // mNextCapturedBuffers
                    StreamBuffer bAux;
					int orientation;
					orientation = getPictureRotate();
					ALOGD("bAux orientation=%d",orientation);
					if ((orientation==90)||(orientation==270)) {
						bAux.streamId = 0;
						bAux.width = b.height;
						bAux.height = b.width;
						bAux.format = HAL_PIXEL_FORMAT_RGB_888;
						bAux.stride = b.height;
						bAux.buffer = NULL;
					} else {
                    	bAux.streamId = 0;
                    	bAux.width = b.width;
                    	bAux.height = b.height;
                    	bAux.format = HAL_PIXEL_FORMAT_RGB_888;
                    	bAux.stride = b.width;
                    	bAux.buffer = NULL;
					}
                    // TODO: Reuse these
                    bAux.img = new uint8_t[b.width * b.height * 3];
                    mNextCapturedBuffers->push_back(bAux);
                    break;
                case HAL_PIXEL_FORMAT_YCrCb_420_SP:
                    captureNV21(b.img, gain, b.stride);
                    break;
                case HAL_PIXEL_FORMAT_YV12:
					captureYV12(b.img, gain, b.stride);
					break;
                default:
                    ALOGE("%s: Unknown format %x, no output", __FUNCTION__,
                            b.format);
                    break;
            }
        }
        putback_frame(vinfo);
        mKernelBuffer = NULL;
    }

    ALOGVV("Sensor vertical blanking interval");
    nsecs_t workDoneRealTime = systemTime();
    const nsecs_t timeAccuracy = 2e6; // 2 ms of imprecision is ok
    if (workDoneRealTime < frameEndRealTime - timeAccuracy) {
        timespec t;
        t.tv_sec = (frameEndRealTime - workDoneRealTime)  / 1000000000L;
        t.tv_nsec = (frameEndRealTime - workDoneRealTime) % 1000000000L;

        int ret;
        do {
            ret = nanosleep(&t, &t);
        } while (ret != 0);
    }
    nsecs_t endRealTime = systemTime();
    ALOGVV("Frame cycle took %d ms, target %d ms",
            (int)((endRealTime - startRealTime)/1000000),
            (int)(frameDuration / 1000000));
    return true;
};

int Sensor::getStreamConfigurations(int32_t picSizes[], const int32_t kAvailableFormats[], int size) {
    int res;
    int i, j, k, START;
    int count = 0;
	int pixelfmt;
    struct v4l2_frmsizeenum frmsize;
    char property[PROPERTY_VALUE_MAX];
    unsigned int support_w,support_h;

    support_w = 10000;
    support_h = 10000;
    memset(property, 0, sizeof(property));
    if(property_get("ro.camera.preview.MaxSize", property, NULL) > 0){
        CAMHAL_LOGDB("support Max Preview Size :%s",property);
        if(sscanf(property,"%dx%d",&support_w,&support_h)!=2){
            support_w = 10000;
            support_h = 10000;
        }
    }

	
    memset(&frmsize,0,sizeof(frmsize));
	for (size_t f = 0;f < sizeof(kAvailableFormats)/sizeof(kAvailableFormats[0]);f++) {
		
		frmsize.pixel_format = getOutputFormat(kAvailableFormats[f]);
	}
 
    START = 0;
    for(i=0;;i++, count+=4){
        frmsize.index = i;
        res = ioctl(vinfo->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
        if (res < 0){
            DBG_LOGB("index=%d, break\n", i);
            break;
        }

        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){ //only support this type

            if (0 != (frmsize.discrete.width%16))
                continue;

            if((frmsize.discrete.width > support_w) && (frmsize.discrete.height >support_h))
                continue;

            if (count >= size)
                break;

            picSizes[count+0] = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
            picSizes[count+1] = frmsize.discrete.width;
            picSizes[count+2] = frmsize.discrete.height;
            picSizes[count+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

            if (0 == i)
                continue;

            for (k = count; k > START; k -= 4) {
                if (frmsize.discrete.width * frmsize.discrete.height >
                        picSizes[k - 3] * picSizes[k - 2]) {
                    picSizes[k + 1] = picSizes[k - 3];
                    picSizes[k + 2] = picSizes[k - 2];

                } else {
                    break;
                }
            }
            picSizes[k + 1] = frmsize.discrete.width;
            picSizes[k + 2] = frmsize.discrete.height;
        }

    }

    START = count;
    for(i=0;;i++, count+=4){
        frmsize.index = i;
        res = ioctl(vinfo->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
        if (res < 0){
            DBG_LOGB("index=%d, break\n", i);
            break;
        }

        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){ //only support this type

            if (0 != (frmsize.discrete.width%16))
                continue;

            if((frmsize.discrete.width > support_w) && (frmsize.discrete.height >support_h))
                continue;

            if (count >= size)
                break;

            picSizes[count+0] = HAL_PIXEL_FORMAT_YCbCr_420_888;
            picSizes[count+1] = frmsize.discrete.width;
            picSizes[count+2] = frmsize.discrete.height;
            picSizes[count+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

            if (0 == i)
                continue;

            for (k = count; k > START; k -= 4) {
                if (frmsize.discrete.width * frmsize.discrete.height >
                        picSizes[k - 3] * picSizes[k - 2]) {
                    picSizes[k + 1] = picSizes[k - 3];
                    picSizes[k + 2] = picSizes[k - 2];

                } else {
                    break;
                }
            }
            picSizes[k + 1] = frmsize.discrete.width;
            picSizes[k + 2] = frmsize.discrete.height;
        }

    }

    uint32_t jpgSrcfmt[] = {
        V4L2_PIX_FMT_RGB24,
        V4L2_PIX_FMT_YUYV,
        V4L2_PIX_FMT_MJPEG,
    };
 
    START = count;
    for (j = 0; j<(sizeof(jpgSrcfmt)/sizeof(jpgSrcfmt[0])); j++) {
        memset(&frmsize,0,sizeof(frmsize));
        frmsize.pixel_format = jpgSrcfmt[j];

        for(i=0;;i++, count+=4){
            frmsize.index = i;
            res = ioctl(vinfo->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
            if (res < 0){
                DBG_LOGB("index=%d, break\n", i);
                break;
            }

            if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){ //only support this type

                if (0 != (frmsize.discrete.width%16))
                    continue;

                //if((frmsize.discrete.width > support_w) && (frmsize.discrete.height >support_h))
                //    continue;

                if (count >= size)
                    break;

                picSizes[count+0] = HAL_PIXEL_FORMAT_BLOB;
                picSizes[count+1] = frmsize.discrete.width;
                picSizes[count+2] = frmsize.discrete.height;
                picSizes[count+3] = ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT;

                if (0 == i)
                    continue;

                //TODO insert in descend order
                for (k = count; k > START; k -= 4) {
                    if (frmsize.discrete.width * frmsize.discrete.height >
                            picSizes[k - 3] * picSizes[k - 2]) {
                        picSizes[k + 1] = picSizes[k - 3];
                        picSizes[k + 2] = picSizes[k - 2];

                        picSizes[k - 3] = frmsize.discrete.width;
                        picSizes[k - 2] = frmsize.discrete.height;
                    } else {
                        break;
                    }
                }
            }

        }

        if (frmsize.index > 0)
            break;
    }

    if (frmsize.index == 0)
        CAMHAL_LOGDA("no support pixel fmt for jpeg");

    return count;

}


int Sensor::getStreamConfigurationDurations(int32_t picSizes[], int64_t duration[], int size)
{
    int ret=0; int framerate=0; int temp_rate=0;
    struct v4l2_frmivalenum fival;
    int i,j=0;
    int count = 0;
    int tmp_size = size;
    memset(duration, 0 ,sizeof(int64_t));
    int pixelfmt_tbl[] = {
		V4L2_PIX_FMT_YVU420,
		V4L2_PIX_FMT_NV21,
        V4L2_PIX_FMT_RGB24,
        //	V4L2_PIX_FMT_MJPEG,
        //	V4L2_PIX_FMT_YUYV,
        //	V4L2_PIX_FMT_YVU420
    };

    for( i = 0; i < (int) ARRAY_SIZE(pixelfmt_tbl); i++)
    {
        for( ; size > 0; size-=4)
        {
            memset(&fival, 0, sizeof(fival));

            for (fival.index = 0;;fival.index++)
            {
                fival.pixel_format = pixelfmt_tbl[i];
                fival.width = picSizes[size-3];
                fival.height = picSizes[size-2];
                if((ret = ioctl(vinfo->fd, VIDIOC_ENUM_FRAMEINTERVALS, &fival)) == 0) {
                    if (fival.type == V4L2_FRMIVAL_TYPE_DISCRETE){
                        temp_rate = fival.discrete.denominator/fival.discrete.numerator;
                        if(framerate < temp_rate)
                            framerate = temp_rate;
                        duration[count+0] = (int64_t)(picSizes[size-4]);
                        duration[count+1] = (int64_t)(picSizes[size-3]);
                        duration[count+2] = (int64_t)(picSizes[size-2]);
                        duration[count+3] = (int64_t)16333333L;//(int64_t)(framerate), here we can get frame interval from camera driver
                        j++;
                    } else if (fival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS){
                        temp_rate = fival.discrete.denominator/fival.discrete.numerator;
                        if(framerate < temp_rate)
                            framerate = temp_rate;
                        duration[count+0] = (int64_t)picSizes[size-4];
                        duration[count+1] = (int64_t)picSizes[size-3];
                        duration[count+2] = (int64_t)picSizes[size-2];
                        duration[count+3] = (int64_t)16333333L;//(int64_t)(framerate), here we can get frame interval from camera driver
                        j++;
                    } else if (fival.type == V4L2_FRMIVAL_TYPE_STEPWISE){
                        temp_rate = fival.discrete.denominator/fival.discrete.numerator;
                        if(framerate < temp_rate)
                            framerate = temp_rate;
                        duration[count+0] = (int64_t)picSizes[size-4];
                        duration[count+1] = (int64_t)picSizes[size-3];
                        duration[count+2] = (int64_t)picSizes[size-2];
                        duration[count+3] = (int64_t)16333333L;//(int64_t)(framerate), here we can get frame interval from camera driver
                        j++;
                    }
                } else {
                    if (j > 0) {
                        duration[count+0] = (int64_t)(picSizes[size-4]);
                        duration[count+1] = (int64_t)(picSizes[size-3]);
                        duration[count+2] = (int64_t)(picSizes[size-2]);
                        duration[count+3] = (int64_t)16333333L;//(int64_t)(framerate), here we can get frame interval from camera driver
                        count += 4;
                        break;
                    } else {
                        break;
                    }
                }
            }
            j=0;
        }
        size = tmp_size;
    }

    return count;

}

int Sensor::getPictureSizes(int32_t picSizes[], int size, bool preview) {
    int res;
    int i;
    int count = 0;
    struct v4l2_frmsizeenum frmsize;
    char property[PROPERTY_VALUE_MAX];
    unsigned int support_w,support_h;
    int preview_fmt;

    support_w = 10000;
    support_h = 10000;
    memset(property, 0, sizeof(property));
    if(property_get("ro.camera.preview.MaxSize", property, NULL) > 0){
        CAMHAL_LOGDB("support Max Preview Size :%s",property);
        if(sscanf(property,"%dx%d",&support_w,&support_h)!=2){
            support_w = 10000;
            support_h = 10000;
        }
    }


    memset(&frmsize,0,sizeof(frmsize));
    preview_fmt = V4L2_PIX_FMT_NV21;//getOutputFormat();

    if (preview_fmt == V4L2_PIX_FMT_MJPEG)
        frmsize.pixel_format = V4L2_PIX_FMT_MJPEG;
    else if (preview_fmt == V4L2_PIX_FMT_NV21) {
        if (preview == true)
            frmsize.pixel_format = V4L2_PIX_FMT_NV21;
        else
            frmsize.pixel_format = V4L2_PIX_FMT_RGB24;
    } else if (preview_fmt == V4L2_PIX_FMT_YVU420) {
        if (preview == true)
            frmsize.pixel_format = V4L2_PIX_FMT_YVU420;
        else
            frmsize.pixel_format = V4L2_PIX_FMT_RGB24;
    } else if (preview_fmt == V4L2_PIX_FMT_YUYV)
        frmsize.pixel_format = V4L2_PIX_FMT_YUYV;

    for(i=0;;i++, count += 2){
        frmsize.index = i;
        res = ioctl(vinfo->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
        if (res < 0){
            DBG_LOGB("index=%d, break\n", i);
            break;
        }


        if(frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE){ //only support this type

            if (0 != (frmsize.discrete.width%16))
                continue;

            if((frmsize.discrete.width > support_w) && (frmsize.discrete.height >support_h))
                continue;

            if (count >= size)
                break;

            picSizes[count] = frmsize.discrete.width;
            picSizes[count+1]  =  frmsize.discrete.height;

            if (0 == i)
                continue;

            //TODO insert in descend order
            if (picSizes[count + 0] * picSizes[count + 1] > picSizes[count - 1] * picSizes[count - 2]) {
                picSizes[count + 0] = picSizes[count - 2];
                picSizes[count + 1] = picSizes[count - 1];

                picSizes[count - 2] = frmsize.discrete.width;
                picSizes[count - 1] = frmsize.discrete.height;
            }

        }

    }

    return count;

}

void Sensor::captureRaw(uint8_t *img, uint32_t gain, uint32_t stride) {
    float totalGain = gain/100.0 * kBaseGainFactor;
    float noiseVarGain =  totalGain * totalGain;
    float readNoiseVar = kReadNoiseVarBeforeGain * noiseVarGain
            + kReadNoiseVarAfterGain;

    int bayerSelect[4] = {Scene::R, Scene::Gr, Scene::Gb, Scene::B}; // RGGB
    mScene.setReadoutPixel(0,0);
    for (unsigned int y = 0; y < kResolution[1]; y++ ) {
        int *bayerRow = bayerSelect + (y & 0x1) * 2;
        uint16_t *px = (uint16_t*)img + y * stride;
        for (unsigned int x = 0; x < kResolution[0]; x++) {
            uint32_t electronCount;
            electronCount = mScene.getPixelElectrons()[bayerRow[x & 0x1]];

            // TODO: Better pixel saturation curve?
            electronCount = (electronCount < kSaturationElectrons) ?
                    electronCount : kSaturationElectrons;

            // TODO: Better A/D saturation curve?
            uint16_t rawCount = electronCount * totalGain;
            rawCount = (rawCount < kMaxRawValue) ? rawCount : kMaxRawValue;

            // Calculate noise value
            // TODO: Use more-correct Gaussian instead of uniform noise
            float photonNoiseVar = electronCount * noiseVarGain;
            float noiseStddev = sqrtf_approx(readNoiseVar + photonNoiseVar);
            // Scaled to roughly match gaussian/uniform noise stddev
            float noiseSample = std::rand() * (2.5 / (1.0 + RAND_MAX)) - 1.25;

            rawCount += kBlackLevel;
            rawCount += noiseStddev * noiseSample;

            *px++ = rawCount;
        }
        // TODO: Handle this better
        //simulatedTime += kRowReadoutTime;
    }
    ALOGVV("Raw sensor image captured");
}

void Sensor::captureRGBA(uint8_t *img, uint32_t gain, uint32_t stride) {
    float totalGain = gain/100.0 * kBaseGainFactor;
    // In fixed-point math, calculate total scaling from electrons to 8bpp
    int scale64x = 64 * totalGain * 255 / kMaxRawValue;
    uint32_t inc = kResolution[0] / stride;

    for (unsigned int y = 0, outY = 0; y < kResolution[1]; y+=inc, outY++ ) {
        uint8_t *px = img + outY * stride * 4;
        mScene.setReadoutPixel(0, y);
        for (unsigned int x = 0; x < kResolution[0]; x+=inc) {
            uint32_t rCount, gCount, bCount;
            // TODO: Perfect demosaicing is a cheat
            const uint32_t *pixel = mScene.getPixelElectrons();
            rCount = pixel[Scene::R]  * scale64x;
            gCount = pixel[Scene::Gr] * scale64x;
            bCount = pixel[Scene::B]  * scale64x;

            *px++ = rCount < 255*64 ? rCount / 64 : 255;
            *px++ = gCount < 255*64 ? gCount / 64 : 255;
            *px++ = bCount < 255*64 ? bCount / 64 : 255;
            *px++ = 255;
            for (unsigned int j = 1; j < inc; j++)
                mScene.getPixelElectrons();
        }
        // TODO: Handle this better
        //simulatedTime += kRowReadoutTime;
    }
    ALOGVV("RGBA sensor image captured");
}

void Sensor::captureRGB(uint8_t *img, uint32_t gain, uint32_t stride) {
#if 0
    float totalGain = gain/100.0 * kBaseGainFactor;
    // In fixed-point math, calculate total scaling from electrons to 8bpp
    int scale64x = 64 * totalGain * 255 / kMaxRawValue;
    uint32_t inc = kResolution[0] / stride;

    for (unsigned int y = 0, outY = 0; y < kResolution[1]; y += inc, outY++ ) {
        mScene.setReadoutPixel(0, y);
        uint8_t *px = img + outY * stride * 3;
        for (unsigned int x = 0; x < kResolution[0]; x += inc) {
            uint32_t rCount, gCount, bCount;
            // TODO: Perfect demosaicing is a cheat
            const uint32_t *pixel = mScene.getPixelElectrons();
            rCount = pixel[Scene::R]  * scale64x;
            gCount = pixel[Scene::Gr] * scale64x;
            bCount = pixel[Scene::B]  * scale64x;

            *px++ = rCount < 255*64 ? rCount / 64 : 255;
            *px++ = gCount < 255*64 ? gCount / 64 : 255;
            *px++ = bCount < 255*64 ? bCount / 64 : 255;
            for (unsigned int j = 1; j < inc; j++)
                mScene.getPixelElectrons();
        }
        // TODO: Handle this better
        //simulatedTime += kRowReadoutTime;
    }
#else
    uint8_t *src;
	int ret,rotate;
	int width,height;
	rotate = getPictureRotate();
	width = vinfo->picture.format.fmt.pix.width;
	height = vinfo->picture.format.fmt.pix.height;
	ret = start_picture(vinfo,rotate);
    if (ret < 0)
    {
        ALOGD("start picture failed!");
    }
    while(1)
    {
        src = (uint8_t *)get_picture(vinfo);
        if (NULL == src) {
            usleep(300000);
            continue;
        } else {
            break;
        }
    }
    ALOGD("get picture success !");
    if (vinfo->picture.format.fmt.pix.pixelformat == V4L2_PIX_FMT_RGB24){
		if (vinfo->picture.buf.length == width*height*3) {
				memcpy(img, src, vinfo->picture.buf.length);
			} else {
				rgb24_memcpy( img, src, width, height);
			}
	}
	stop_picture(vinfo);
#endif
}

void Sensor::YUYVToNV21(uint8_t *src, uint8_t *dst, int width, int height)
{
    for (int i = 0; i < width * height * 2; i += 2) {
        *dst++ = *(src + i);
    }

    for (int y = 0; y < height - 1; y +=2) {
        for (int j = 0; j < width * 2; j += 4) {
            *dst++ = (*(src + 3 + j) + *(src + 3 + j + width * 2) + 1) >> 1;    //v
            *dst++ = (*(src + 1 + j) + *(src + 1 + j + width * 2) + 1) >> 1;    //u
        }
        src += width * 2 * 2;
    }

    if (height & 1)
        for (int j = 0; j < width * 2; j += 4) {
            *dst++ = *(src + 3 + j);    //v
            *dst++ = *(src + 1 + j);    //u
        }
}

void Sensor::YUYVToYV12(uint8_t *src, uint8_t *dst, int width, int height)
{
	//width should be an even number.
	//uv ALIGN 32.
	int i,j,stride,c_stride,c_size,y_size,cb_offset,cr_offset;
	unsigned char *dst_copy,*src_copy;

	dst_copy = dst;
	src_copy = src;

	y_size = width*height;
	c_stride = ALIGN(width/2, 16);
	c_size = c_stride * height/2;
	cr_offset = y_size;
	cb_offset = y_size+c_size;

	for(i=0;i< y_size;i++){
		*dst++ = *src;
		src += 2;
	}

	dst = dst_copy;
	src = src_copy;

	for(i=0;i<height;i+=2){
		for(j=1;j<width*2;j+=4){//one line has 2*width bytes for yuyv.
			//ceil(u1+u2)/2
			*(dst+cr_offset+j/4)= (*(src+j+2) + *(src+j+2+width*2) + 1)/2;
			*(dst+cb_offset+j/4)= (*(src+j) + *(src+j+width*2) + 1)/2;
		}
		dst += c_stride;
		src += width*4;
	}
}


void Sensor::captureNV21(uint8_t *img, uint32_t gain, uint32_t stride) {
#if 0
    float totalGain = gain/100.0 * kBaseGainFactor;
    // Using fixed-point math with 6 bits of fractional precision.
    // In fixed-point math, calculate total scaling from electrons to 8bpp
    const int scale64x = 64 * totalGain * 255 / kMaxRawValue;
    // In fixed-point math, saturation point of sensor after gain
    const int saturationPoint = 64 * 255;
    // Fixed-point coefficients for RGB-YUV transform
    // Based on JFIF RGB->YUV transform.
    // Cb/Cr offset scaled by 64x twice since they're applied post-multiply
    const int rgbToY[]  = {19, 37, 7};
    const int rgbToCb[] = {-10,-21, 32, 524288};
    const int rgbToCr[] = {32,-26, -5, 524288};
    // Scale back to 8bpp non-fixed-point
    const int scaleOut = 64;
    const int scaleOutSq = scaleOut * scaleOut; // after multiplies

    uint32_t inc = kResolution[0] / stride;
    uint32_t outH = kResolution[1] / inc;
    for (unsigned int y = 0, outY = 0;
         y < kResolution[1]; y+=inc, outY++) {
        uint8_t *pxY = img + outY * stride;
        uint8_t *pxVU = img + (outH + outY / 2) * stride;
        mScene.setReadoutPixel(0,y);
        for (unsigned int outX = 0; outX < stride; outX++) {
            int32_t rCount, gCount, bCount;
            // TODO: Perfect demosaicing is a cheat
            const uint32_t *pixel = mScene.getPixelElectrons();
            rCount = pixel[Scene::R]  * scale64x;
            rCount = rCount < saturationPoint ? rCount : saturationPoint;
            gCount = pixel[Scene::Gr] * scale64x;
            gCount = gCount < saturationPoint ? gCount : saturationPoint;
            bCount = pixel[Scene::B]  * scale64x;
            bCount = bCount < saturationPoint ? bCount : saturationPoint;

            *pxY++ = (rgbToY[0] * rCount +
                    rgbToY[1] * gCount +
                    rgbToY[2] * bCount) / scaleOutSq;
            if (outY % 2 == 0 && outX % 2 == 0) {
                *pxVU++ = (rgbToCr[0] * rCount +
                        rgbToCr[1] * gCount +
                        rgbToCr[2] * bCount +
                        rgbToCr[3]) / scaleOutSq;
                *pxVU++ = (rgbToCb[0] * rCount +
                        rgbToCb[1] * gCount +
                        rgbToCb[2] * bCount +
                        rgbToCb[3]) / scaleOutSq;
            }
            for (unsigned int j = 1; j < inc; j++)
                mScene.getPixelElectrons();
        }
    }
#else
    uint8_t *src;
    if (mKernelBuffer) {
        src = mKernelBuffer;
        if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_NV21)
            memcpy(img, src, vinfo->preview.buf.length);
        else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
            int width = vinfo->preview.format.fmt.pix.width;
            int height = vinfo->preview.format.fmt.pix.height;
            YUYVToNV21(src, img, width, height);
        }
        else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
            int width = vinfo->preview.format.fmt.pix.width;
            int height = vinfo->preview.format.fmt.pix.height;
            if (ConvertMjpegToNV21(src, vinfo->preview.buf.bytesused, img,
                        width, img + width * height, (width + 1) / 2, width,
                        height, width, height, libyuv::FOURCC_MJPG) != 0) {
                DBG_LOGA("Decode MJPEG frame failed\n");
            }
        } else {
            ALOGE("Unable known sensor format: %d", vinfo->preview.format.fmt.pix.pixelformat);
        }
        return ;
    }
    while(1){
            src = (uint8_t *)get_frame(vinfo);
            usleep(30000);
            if (NULL == src)
                    continue;
            if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_NV21)
                memcpy(img, src, vinfo->preview.buf.length);
            else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
                int width = vinfo->preview.format.fmt.pix.width;
                int height = vinfo->preview.format.fmt.pix.height;
                YUYVToNV21(src, img, width, height);
            }
            else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
                int width = vinfo->preview.format.fmt.pix.width;
                int height = vinfo->preview.format.fmt.pix.height;
                if (ConvertMjpegToNV21(src, vinfo->preview.buf.bytesused, img,
                        width, img + width * height, (width + 1) / 2, width,
                        height, width, height, libyuv::FOURCC_MJPG) != 0) {
                    DBG_LOGA("Decode MJPEG frame failed\n");
                }
            } else {
                ALOGE("Unable known sensor format: %d", vinfo->preview.format.fmt.pix.pixelformat);
            }

            break;
    }
#endif
    mKernelBuffer = src;
    ALOGVV("NV21 sensor image captured");
}

void Sensor::captureYV12(uint8_t *img, uint32_t gain, uint32_t stride) {
#if 0
    float totalGain = gain/100.0 * kBaseGainFactor;
    // Using fixed-point math with 6 bits of fractional precision.
    // In fixed-point math, calculate total scaling from electrons to 8bpp
    const int scale64x = 64 * totalGain * 255 / kMaxRawValue;
    // In fixed-point math, saturation point of sensor after gain
    const int saturationPoint = 64 * 255;
    // Fixed-point coefficients for RGB-YUV transform
    // Based on JFIF RGB->YUV transform.
    // Cb/Cr offset scaled by 64x twice since they're applied post-multiply
    const int rgbToY[]  = {19, 37, 7};
    const int rgbToCb[] = {-10,-21, 32, 524288};
    const int rgbToCr[] = {32,-26, -5, 524288};
    // Scale back to 8bpp non-fixed-point
    const int scaleOut = 64;
    const int scaleOutSq = scaleOut * scaleOut; // after multiplies

    uint32_t inc = kResolution[0] / stride;
    uint32_t outH = kResolution[1] / inc;
    for (unsigned int y = 0, outY = 0;
         y < kResolution[1]; y+=inc, outY++) {
        uint8_t *pxY = img + outY * stride;
        uint8_t *pxVU = img + (outH + outY / 2) * stride;
        mScene.setReadoutPixel(0,y);
        for (unsigned int outX = 0; outX < stride; outX++) {
            int32_t rCount, gCount, bCount;
            // TODO: Perfect demosaicing is a cheat
            const uint32_t *pixel = mScene.getPixelElectrons();
            rCount = pixel[Scene::R]  * scale64x;
            rCount = rCount < saturationPoint ? rCount : saturationPoint;
            gCount = pixel[Scene::Gr] * scale64x;
            gCount = gCount < saturationPoint ? gCount : saturationPoint;
            bCount = pixel[Scene::B]  * scale64x;
            bCount = bCount < saturationPoint ? bCount : saturationPoint;

            *pxY++ = (rgbToY[0] * rCount +
                    rgbToY[1] * gCount +
                    rgbToY[2] * bCount) / scaleOutSq;
            if (outY % 2 == 0 && outX % 2 == 0) {
                *pxVU++ = (rgbToCr[0] * rCount +
                        rgbToCr[1] * gCount +
                        rgbToCr[2] * bCount +
                        rgbToCr[3]) / scaleOutSq;
                *pxVU++ = (rgbToCb[0] * rCount +
                        rgbToCb[1] * gCount +
                        rgbToCb[2] * bCount +
                        rgbToCb[3]) / scaleOutSq;
            }
            for (unsigned int j = 1; j < inc; j++)
                mScene.getPixelElectrons();
        }
    }
#else
    uint8_t *src;
    if (mKernelBuffer) {
        src = mKernelBuffer;
		if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YVU420)
            memcpy(img, src, vinfo->preview.buf.length);
        else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
            int width = vinfo->preview.format.fmt.pix.width;
            int height = vinfo->preview.format.fmt.pix.height;
            YUYVToYV12(src, img, width, height);
        }
        else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
            int width = vinfo->preview.format.fmt.pix.width;
            int height = vinfo->preview.format.fmt.pix.height;
            if (ConvertToI420(src, vinfo->preview.buf.bytesused, img, width, img + width * height + width * height / 4, (width + 1) / 2,
						img + width * height, (width + 1) / 2, 0, 0, width, height,
						width, height, libyuv::kRotate0, libyuv::FOURCC_MJPG) != 0) {
                DBG_LOGA("Decode MJPEG frame failed\n");
            }
        } else {
            ALOGE("Unable known sensor format: %d", vinfo->preview.format.fmt.pix.pixelformat);
        }
        return ;
    }
    while(1){
            src = (uint8_t *)get_frame(vinfo);
            usleep(30000);
            if (NULL == src)
                    continue;
			if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YVU420) {
				memcpy(img, src, vinfo->preview.buf.length);
			}
			
            else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
                int width = vinfo->preview.format.fmt.pix.width;
                int height = vinfo->preview.format.fmt.pix.height;
                YUYVToYV12(src, img, width, height);
            }
            else if (vinfo->preview.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) {
                int width = vinfo->preview.format.fmt.pix.width;
                int height = vinfo->preview.format.fmt.pix.height;
                if (ConvertToI420(src, vinfo->preview.buf.bytesused, img, width, img + width * height + width * height / 4, (width + 1) / 2,
						img + width * height, (width + 1) / 2, 0, 0, width, height,
						width, height, libyuv::kRotate0, libyuv::FOURCC_MJPG) != 0) {
                    DBG_LOGA("Decode MJPEG frame failed\n");
                }
            } else {
                ALOGE("Unable known sensor format: %d", vinfo->preview.format.fmt.pix.pixelformat);
            }

            break;
    }
#endif
    mKernelBuffer = src;
    ALOGVV("YV12 sensor image captured");
}

} // namespace android

