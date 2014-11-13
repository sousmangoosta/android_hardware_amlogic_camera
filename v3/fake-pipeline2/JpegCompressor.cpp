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
#define LOG_TAG "EmulatedCamera2_JpegCompressor"

#include <utils/Log.h>
#include <ui/GraphicBufferMapper.h>

#include "JpegCompressor.h"
#include "../EmulatedFakeCamera2.h"
#include "../EmulatedFakeCamera3.h"
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>

#define ARRAY_SIZE(array) (sizeof((array)) / sizeof((array)[0]))
const size_t MARKER_LENGTH = 2; // length of a marker
const uint8_t MARK = 0xFF;
const uint8_t EOI = 0xD9;
bool checkJpegEnd(uint8_t *buf) {
    return buf[0] == MARK && buf[1] == EOI;
}
int extraSmallImg(unsigned char* SrcImg,int SrcW,int SrcH,
                unsigned char* DstImg,int DstW,int DstH)
{
    int skipW = SrcW/DstW;
	int skipH = SrcH/DstH;
	unsigned char* dst = DstImg;
	unsigned char* srcrow = SrcImg;
	unsigned char* srcrowidx = srcrow;
	int i = 0,j = 0;
	for(;i<DstH;i++)
	{
		for(j = 0;j<DstW;j++)
		{
			dst[0] = srcrowidx[0];
			dst[1] = srcrowidx[1];
			dst[2] = srcrowidx[2];
			dst+=3;
			srcrowidx+=3*skipW;
		}
		srcrow += skipH*SrcW*3;
		srcrowidx = srcrow;
	}
	return 1;
}
namespace android {

struct string_pair {
    const char* string1;
    const char* string2;
};
static string_pair degress_to_exif_lut [] = {
    {"0",   "1"},
    {"90",  "6"},
    {"180", "3"},
    {"270", "8"},
};
JpegCompressor::JpegCompressor():
        Thread(false),
        mIsBusy(false),
        mSynchronous(false),
        mNeedexif(true),
        mNeedThumbnail(false),
        mMainJpegSize(0),
        mThumbJpegSize(0),
        mSrcThumbBuffer(NULL),
		mDstThumbBuffer(NULL),
        mBuffers(NULL),
        mListener(NULL) {
        memset(&mInfo,0,sizeof(struct ExifInfo));
}

JpegCompressor::~JpegCompressor() {
    Mutex::Autolock lock(mMutex);
}

status_t JpegCompressor::start(Buffers *buffers, JpegListener *listener) {
    if (listener == NULL) {
        ALOGE("%s: NULL listener not allowed!", __FUNCTION__);
        return BAD_VALUE;
    }
    Mutex::Autolock lock(mMutex);
    {
        Mutex::Autolock busyLock(mBusyMutex);

        if (mIsBusy) {
            ALOGE("%s: Already processing a buffer!", __FUNCTION__);
            return INVALID_OPERATION;
        }

        mIsBusy = true;
        mSynchronous = false;
        mBuffers = buffers;
        mListener = listener;
    }

    status_t res;
    res = run("EmulatedFakeCamera2::JpegCompressor");
    if (res != OK) {
        ALOGE("%s: Unable to start up compression thread: %s (%d)",
                __FUNCTION__, strerror(-res), res);
        delete mBuffers;
    }
    return res;
}

status_t JpegCompressor::compressSynchronous(Buffers *buffers) {
    status_t res;

    Mutex::Autolock lock(mMutex);
    {
        Mutex::Autolock busyLock(mBusyMutex);

        if (mIsBusy) {
            ALOGE("%s: Already processing a buffer!", __FUNCTION__);
            return INVALID_OPERATION;
        }

        mIsBusy = true;
        mSynchronous = true;
        mBuffers = buffers;
    }

    res = compress();

    cleanUp();

    return res;
}

status_t JpegCompressor::cancel() {
    requestExitAndWait();
    return OK;
}

status_t JpegCompressor::readyToRun() {
    return OK;
}

bool JpegCompressor::threadLoop() {
    status_t res;
	struct timeval mTimeStart,mTimeend;
	int intreval;
	ExifElementsTable* exiftable = NULL;
	struct camera2_jpeg_blob blob;
	int offset;
    ALOGV("%s: Starting compression thread", __FUNCTION__);

	gettimeofday(&mTimeStart, NULL);
    res = compress();
	if (mNeedexif) {
		memset(&blob,0,sizeof(struct camera2_jpeg_blob));
		exiftable = new ExifElementsTable();
		GenExif(exiftable);
	}
	if (mNeedThumbnail) {
		res = thumbcompress();
	}
	
	if (exiftable) {
        uint32_t realjpegsize = 0;
		Section_t* exif_section = NULL;
		ExifElementsTable* exif = exiftable;
		exif->insertExifToJpeg((unsigned char*)mJpegBuffer.img,mMainJpegSize);
        if ((mNeedThumbnail)&&(mDstThumbBuffer != NULL)) {
		exif->insertExifThumbnailImage((const char*)mDstThumbBuffer,mThumbJpegSize);
        }
		exif_section = FindSection(M_EXIF);
		if (exif_section) {
			exif->saveJpeg((unsigned char*) mJpegBuffer.img, mMainJpegSize + exif_section->Size);
		}
        for (uint32_t size = (mMainJpegSize + exif_section->Size - 2); size > 0; size--) {
            if (checkJpegEnd(mJpegBuffer.img + size)) {
                realjpegsize = (size + MARKER_LENGTH);
                break;
            }
        }
		int offset = mMaxbufsize-sizeof(struct camera2_jpeg_blob);
		blob.jpeg_blob_id = 0x00FF;
		blob.jpeg_size = realjpegsize;
		memcpy(mJpegBuffer.img+offset, &blob, sizeof(struct camera2_jpeg_blob));
	}
    mListener->onJpegDone(mJpegBuffer, res == OK);
	
	if (mNeedexif) {
		if (exiftable != NULL) {
			delete exiftable;
			exiftable = NULL;
		}
	}
	gettimeofday(&mTimeend, NULL);
	intreval = (mTimeend.tv_sec - mTimeStart.tv_sec) * 1000 + ((mTimeend.tv_usec - mTimeStart.tv_usec))/1000;
	ALOGD("jpeg compress cost time =%d ms",intreval);
    cleanUp();

    return false;
}

status_t JpegCompressor::compress() {
    // Find source and target buffers. Assumes only one buffer matches
    // each condition!
    //Mutex::Autolock lock(mMutex);
    bool foundJpeg = false, mFoundAux = false;
    for (size_t i = 0; i < mBuffers->size(); i++) {
        const StreamBuffer &b = (*mBuffers)[i];
        if (b.format == HAL_PIXEL_FORMAT_BLOB) {
            mJpegBuffer = b;
            mFoundJpeg = true;
        } else if (b.streamId <= 0) {
            mAuxBuffer = b;
            mFoundAux = true;
        }
        if (mFoundJpeg && mFoundAux) break;
    }
    if (!mFoundJpeg || !mFoundAux) {
        ALOGE("%s: Unable to find buffers for JPEG source/destination",
                __FUNCTION__);
        return BAD_VALUE;
    }

	if (mNeedThumbnail == true) {
		if (mSrcThumbBuffer == NULL) {
		mSrcThumbBuffer = (uint8_t*)malloc(mInfo.thumbwidth*mInfo.thumbheight*3);
		}
		if (mDstThumbBuffer == NULL) {
		mDstThumbBuffer = (uint8_t*)malloc(mInfo.thumbwidth*mInfo.thumbheight*3);
		}
		if (mSrcThumbBuffer) {
			if (mAuxBuffer.format == HAL_PIXEL_FORMAT_RGB_888)
				extraSmallImg(mAuxBuffer.img,mAuxBuffer.width,mAuxBuffer.height,
							  mSrcThumbBuffer,mInfo.thumbwidth,mInfo.thumbheight);
		}
	}

    // Set up error management

    mJpegErrorInfo = NULL;
    JpegError error;
    error.parent = this;

    mCInfo.err = jpeg_std_error(&error);
    mCInfo.err->error_exit = MainJpegErrorHandler;

    jpeg_create_compress(&mCInfo);
    if (checkError("Error initializing compression")) return NO_INIT;

    // Route compressed data straight to output stream buffer

    JpegDestination jpegDestMgr;
    jpegDestMgr.parent = this;
    jpegDestMgr.init_destination = MainJpegInitDestination;
    jpegDestMgr.empty_output_buffer = MainJpegEmptyOutputBuffer;
    jpegDestMgr.term_destination = MainJpegTermDestination;

    mCInfo.dest = &jpegDestMgr;

    // Set up compression parameters

    mCInfo.image_width = mAuxBuffer.width;
    mCInfo.image_height = mAuxBuffer.height;
    mCInfo.input_components = 3;
    mCInfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&mCInfo);
    if (checkError("Error configuring defaults")) return NO_INIT;

    // Do compression

    jpeg_start_compress(&mCInfo, TRUE);
    if (checkError("Error starting compression")) return NO_INIT;

    size_t rowStride = mAuxBuffer.stride * 3;
    const size_t kChunkSize = 32;
    while (mCInfo.next_scanline < mCInfo.image_height) {
        JSAMPROW chunk[kChunkSize];
        for (size_t i = 0 ; i < kChunkSize; i++) {
            chunk[i] = (JSAMPROW)
                    (mAuxBuffer.img + (i + mCInfo.next_scanline) * rowStride);
        }
        jpeg_write_scanlines(&mCInfo, chunk, kChunkSize);
        if (checkError("Error while compressing")) return NO_INIT;
        if (exitPending()) {
            ALOGV("%s: Cancel called, exiting early", __FUNCTION__);
            return TIMED_OUT;
        }
    }

    jpeg_finish_compress(&mCInfo);
    if (checkError("Error while finishing compression")) return NO_INIT;

    // All done
	mMainJpegSize = kMaxJpegSize - mCInfo.dest->free_in_buffer;
	ALOGD("mMainJpegSize = %d",mMainJpegSize);


    return OK;
}

status_t JpegCompressor::thumbcompress() {
	if ((mSrcThumbBuffer == NULL)||(mDstThumbBuffer == NULL))
			return 0;
	//Mutex::Autolock lock(mMutex);
    mJpegErrorInfo = NULL;
    JpegError error;
    error.parent = this;
    mCInfo.err = jpeg_std_error(&error);
    mCInfo.err->error_exit = ThumbJpegErrorHandler;

    jpeg_create_compress(&mCInfo);
    if (checkError("Error initializing compression")) return NO_INIT;
    JpegDestination jpegDestMgr;
    jpegDestMgr.parent = this;
    jpegDestMgr.init_destination = ThumbJpegInitDestination;
    jpegDestMgr.empty_output_buffer = ThumbJpegEmptyOutputBuffer;
    jpegDestMgr.term_destination = ThumbJpegTermDestination;
    mCInfo.dest = &jpegDestMgr;

    // Set up compression parameters

    mCInfo.image_width = mInfo.thumbwidth;
    mCInfo.image_height = mInfo.thumbheight;
    mCInfo.input_components = 3;
    mCInfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&mCInfo);
    if (checkError("Error configuring defaults")) return NO_INIT;
    jpeg_start_compress(&mCInfo, TRUE);
    if (checkError("Error starting compression")) return NO_INIT;
    size_t rowStride = mInfo.thumbwidth* 3;
    const size_t kChunkSize = 32;
    while (mCInfo.next_scanline < mCInfo.image_height) {
        JSAMPROW chunk[kChunkSize];
        for (size_t i = 0 ; i < kChunkSize; i++) {
            chunk[i] = (JSAMPROW)
                    (mSrcThumbBuffer + (i + mCInfo.next_scanline) * rowStride);
        }
        jpeg_write_scanlines(&mCInfo, chunk, kChunkSize);
        if (checkError("Error while compressing")) return NO_INIT;
        if (exitPending()) {
            ALOGV("%s: Cancel called, exiting early", __FUNCTION__);
            return TIMED_OUT;
        }
    }
    jpeg_finish_compress(&mCInfo);
    if (checkError("Error while finishing compression")) return NO_INIT;
	mThumbJpegSize = kMaxJpegSize - mCInfo.dest->free_in_buffer;
	ALOGD("mThumbJpegSize = %d",mThumbJpegSize);
    return OK;
}
bool JpegCompressor::isBusy() {
    Mutex::Autolock busyLock(mBusyMutex);
    return mIsBusy;
}

bool JpegCompressor::isStreamInUse(uint32_t id) {
    Mutex::Autolock lock(mBusyMutex);

    if (mBuffers && mIsBusy) {
        for (size_t i = 0; i < mBuffers->size(); i++) {
            if ( (*mBuffers)[i].streamId == (int)id ) return true;
        }
    }
    return false;
}

bool JpegCompressor::waitForDone(nsecs_t timeout) {
    Mutex::Autolock lock(mBusyMutex);
    status_t res = OK;
    if (mIsBusy) {
        res = mDone.waitRelative(mBusyMutex, timeout);
    }
    return (res == OK);
}

bool JpegCompressor::checkError(const char *msg) {
    if (mJpegErrorInfo) {
        char errBuffer[JMSG_LENGTH_MAX];
        mJpegErrorInfo->err->format_message(mJpegErrorInfo, errBuffer);
        ALOGE("%s: %s: %s",
                __FUNCTION__, msg, errBuffer);
        mJpegErrorInfo = NULL;
        return true;
    }
    return false;
}

void JpegCompressor::cleanUp() {
    status_t res;
    jpeg_destroy_compress(&mCInfo);
    Mutex::Autolock lock(mBusyMutex);
	if (mNeedThumbnail == true) {
		mNeedThumbnail = false;
		if (mSrcThumbBuffer != NULL) {
			free(mSrcThumbBuffer);
			mSrcThumbBuffer = NULL;
		}
		if (mDstThumbBuffer != NULL) {
			free(mDstThumbBuffer);
			mDstThumbBuffer = NULL;
		}
	}
    if (mFoundAux) {
        if (mAuxBuffer.streamId == 0) {
            delete[] mAuxBuffer.img;
        } else if (!mSynchronous) {
            mListener->onJpegInputDone(mAuxBuffer);
        }
    }
    if (!mSynchronous) {
        delete mBuffers;
    }

    mBuffers = NULL;

    mIsBusy = false;
    mDone.signal();
}

void JpegCompressor::MainJpegErrorHandler(j_common_ptr cinfo) {
    JpegError *error = static_cast<JpegError*>(cinfo->err);
    error->parent->mJpegErrorInfo = cinfo;
}

void JpegCompressor::MainJpegInitDestination(j_compress_ptr cinfo) {
    JpegDestination *dest= static_cast<JpegDestination*>(cinfo->dest);
    ALOGV("%s: Setting destination to %p, size %zu",
            __FUNCTION__, dest->parent->mJpegBuffer.img, kMaxJpegSize);
    dest->next_output_byte = (JOCTET*)(dest->parent->mJpegBuffer.img);
    dest->free_in_buffer = kMaxJpegSize;
}

boolean JpegCompressor::MainJpegEmptyOutputBuffer(j_compress_ptr cinfo) {
    ALOGE("%s: JPEG destination buffer overflow!",
            __FUNCTION__);
    return true;
}

void JpegCompressor::MainJpegTermDestination(j_compress_ptr cinfo) {
    ALOGV("%s: Done writing JPEG data. %zu bytes left in buffer",
            __FUNCTION__, cinfo->dest->free_in_buffer);
}

void JpegCompressor::ThumbJpegErrorHandler(j_common_ptr cinfo) {
    JpegError *error = static_cast<JpegError*>(cinfo->err);
    error->parent->mJpegErrorInfo = cinfo;
}
void JpegCompressor::ThumbJpegInitDestination(j_compress_ptr cinfo) {
    JpegDestination *dest= static_cast<JpegDestination*>(cinfo->dest);
    ALOGV("%s: Setting destination to %p, size %zu",
            __FUNCTION__, dest->parent->mDstThumbBuffer, kMaxJpegSize);
    dest->next_output_byte = (JOCTET*)(dest->parent->mDstThumbBuffer);
    dest->free_in_buffer = kMaxJpegSize;
}
boolean JpegCompressor::ThumbJpegEmptyOutputBuffer(j_compress_ptr cinfo) {
    ALOGE("%s: Thumb JPEG destination buffer overflow!",
            __FUNCTION__);
    return true;
}
void JpegCompressor::ThumbJpegTermDestination(j_compress_ptr cinfo) {
    ALOGV("%s: Done writing JPEG data. %zu bytes left in buffer",
            __FUNCTION__, cinfo->dest->free_in_buffer);
}
JpegCompressor::JpegListener::~JpegListener() {
}

void JpegCompressor::SetMaxJpegBufferSize(ssize_t size)
{
    mMaxbufsize = size;
}
ssize_t JpegCompressor::GetMaxJpegBufferSize()
{
    return mMaxbufsize;
}
void JpegCompressor::SetExifInfo(struct ExifInfo info)
{
	mInfo.mainwidth = info.mainwidth;
	mInfo.mainheight = info.mainheight;
	mInfo.thumbwidth = info.thumbwidth;
	mInfo.thumbheight = info.thumbheight;
	mInfo.gpsTimestamp = info.gpsTimestamp;
	mInfo.latitude = info.latitude;
	mInfo.longitude = info.longitude;
	mInfo.altitude = info.altitude;
	mInfo.gpsProcessingMethod = info.gpsProcessingMethod;
	mInfo.focallen = info.focallen;
	mInfo.orientation = info.orientation;
	mInfo.has_latitude = info.has_latitude;
	mInfo.has_longitude = info.has_longitude;
	mInfo.has_altitude = info.has_altitude;
	mInfo.has_gpsProcessingMethod = info.has_gpsProcessingMethod;
	mInfo.has_gpsTimestamp = info.has_gpsTimestamp;
	mInfo.has_focallen = info.has_focallen;
	if ((mInfo.thumbwidth>0)&&(mInfo.thumbheight>0)) {
		mNeedThumbnail = true;
	}
}
int JpegCompressor::GenExif(ExifElementsTable* exiftable)
{
	char exifcontent[256];
	int width,height;
	
	bool newexif = true; //add new exif tag for cts
	float exposuretime = 1.0; 
	float ApertureValue = 1.0;
	int flash = 0;
	int whitebalance = 1;
	int iso = 100;
	char  SubSecTime[10] = "63";
	char  SubSecTimeOrig[10]= "63";
	char  SubSecTimeDig[10]= "63";
	
	exiftable->insertElement("Make","m102");
	exiftable->insertElement("Model","m102");
//	int orientation = mInfo.orientation;
	width = mInfo.mainwidth;
	height = mInfo.mainheight;
#if 0
	if(orientation == 0)
		orientation = 1;
	else if(orientation == 90)
		orientation = 6;
	else if(orientation == 180)
		orientation = 3;
	else if(orientation == 270)
		orientation = 8;	
	sprintf(exifcontent,"%d",orientation);
	exiftable->insertElement("Orientation",(const char*)exifcontent);
#endif
	sprintf(exifcontent,"%d",width);
	exiftable->insertElement("ImageWidth",(const char*)exifcontent);
	sprintf(exifcontent,"%d",height);
	exiftable->insertElement("ImageLength",(const char*)exifcontent);

	sprintf(exifcontent,"%f",exposuretime);
	exiftable->insertElement("ExposureTime",(const char*)exifcontent);
	sprintf(exifcontent,"%f",ApertureValue);
	exiftable->insertElement("ApertureValue",(const char*)exifcontent);
	sprintf(exifcontent,"%d",flash);
	exiftable->insertElement("Flash",(const char*)exifcontent);
	sprintf(exifcontent,"%d",whitebalance);
	exiftable->insertElement("WhiteBalance",(const char*)exifcontent);
	sprintf(exifcontent,"%d",iso);
	exiftable->insertElement("ISOSpeedRatings",(const char*)exifcontent);
	if (newexif) {
		time_t times;
		{
			time(&times);
			struct tm tmstruct;
			tmstruct = *(localtime(&times)); //convert to local time
			strftime(exifcontent, 30, "%Y:%m:%d %H:%M:%S", &tmstruct);
			exiftable->insertElement("DateTimeDigitized",(const char*)exifcontent);
		}
		{
			sprintf(exifcontent, "%s", SubSecTime);
			exiftable->insertElement("SubSecTime",(const char*)exifcontent);
		}
		{

			sprintf(exifcontent, "%s", SubSecTimeOrig);
			exiftable->insertElement("SubSecTimeOriginal",(const char*)exifcontent);
		}
		{

			sprintf(exifcontent, "%s", SubSecTimeDig);
			exiftable->insertElement("SubSecTimeDigitized",(const char*)exifcontent);
		}
	}
	
	if (mInfo.has_focallen) {
		float focallen = mInfo.focallen;
		if(focallen >= 0){
			int focalNum = focallen*1000;
			int focalDen = 1000;
			sprintf(exifcontent,"%d/%d",focalNum,focalDen);
			exiftable->insertElement("FocalLength",(const char*)exifcontent);
		}
	}
	time_t times;
	{
		time(&times);
		struct tm tmstruct;
		tmstruct = *(localtime(&times)); //convert to local time
		strftime(exifcontent, 30, "%Y:%m:%d %H:%M:%S", &tmstruct);
		exiftable->insertElement("DateTime",(const char*)exifcontent);
	}
	if (mInfo.has_gpsTimestamp) {
		times = mInfo.gpsTimestamp;
		if(times != -1){
			struct tm tmstruct;
			tmstruct = *(gmtime(&times));//convert to standard time
			strftime(exifcontent, 20, "%Y:%m:%d", &tmstruct);
			exiftable->insertElement("GPSDateStamp",(const char*)exifcontent);
			sprintf(exifcontent,"%d/%d,%d/%d,%d/%d",tmstruct.tm_hour,1,tmstruct.tm_min,1,tmstruct.tm_sec,1);
			exiftable->insertElement("GPSTimeStamp",(const char*)exifcontent);
		}
	}
	if (mInfo.has_latitude) {
		int offset = 0;
		float latitude = mInfo.latitude;
		if(latitude < 0.0){
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
	if (mInfo.has_longitude) {
		int offset = 0;
		float longitude = mInfo.longitude;
		if(longitude < 0.0){
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
	if (mInfo.has_altitude) {
		int offset = 0;
		float altitude = mInfo.altitude;
		if(altitude < 0.0){
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
	if (mInfo.has_gpsProcessingMethod) {
		char* processmethod = (char*)mInfo.gpsProcessingMethod;
		if(processmethod!=NULL){
			memset(exifcontent,0,sizeof(exifcontent));
			char ExifAsciiPrefix[] = { 0x41, 0x53, 0x43, 0x49, 0x49, 0x0, 0x0, 0x0 };//asicii
			memcpy(exifcontent,ExifAsciiPrefix,8);
			memcpy(exifcontent+8,processmethod,strlen(processmethod));
			exiftable->insertElement("GPSProcessingMethod",(const char*)exifcontent);
		}
	}
	return 1;
}
const char* ExifElementsTable::degreesToExifOrientation(const char* degrees) {
    for (unsigned int i = 0; i < ARRAY_SIZE(degress_to_exif_lut); i++) {
        if (!strcmp(degrees, degress_to_exif_lut[i].string1)) {
            return degress_to_exif_lut[i].string2;
        }
    }
    return NULL;
}
void ExifElementsTable::stringToRational(const char* str, unsigned int* num, unsigned int* den) {
    int len;
    char * tempVal = NULL;
    if (str != NULL) {
        len = strlen(str);
        tempVal = (char*) malloc( sizeof(char) * (len + 1));
    }
    if (tempVal != NULL) {
        size_t den_len;
        char *ctx;
        unsigned int numerator = 0;
        unsigned int denominator = 0;
        char* temp = NULL;
        memset(tempVal, '\0', len + 1);
        strncpy(tempVal, str, len);
        temp = strtok_r(tempVal, ".", &ctx);
        if (temp != NULL)
            numerator = atoi(temp);
        if (!numerator)
            numerator = 1;
        temp = strtok_r(NULL, ".", &ctx);
        if (temp != NULL) {
            den_len = strlen(temp);
            if(HUGE_VAL == den_len ) {
                den_len = 0;
            }
            denominator = static_cast<unsigned int>(pow(10, den_len));
            numerator = numerator * denominator + atoi(temp);
        } else {
            denominator = 1;
        }
        free(tempVal);
        *num = numerator;
        *den = denominator;
    }
}
bool ExifElementsTable::isAsciiTag(const char* tag) {
    return (strcmp(tag, TAG_GPS_PROCESSING_METHOD) == 0);
}
status_t ExifElementsTable::insertElement(const char* tag, const char* value) {
    int value_length = 0;
    status_t ret = NO_ERROR;
    if (!value || !tag) {
        return -EINVAL;
    }
    if (position >= MAX_EXIF_TAGS_SUPPORTED) {
        CAMHAL_LOGEA("Max number of EXIF elements already inserted");
        return NO_MEMORY;
    }
    if (isAsciiTag(tag)) {
        value_length = sizeof(ExifAsciiPrefix) + strlen(value + sizeof(ExifAsciiPrefix));
    } else {
        value_length = strlen(value);
    }
    if (IsGpsTag(tag)) {
        table[position].GpsTag = TRUE;
        table[position].Tag = GpsTagNameToValue(tag);
        gps_tag_count++;
    } else {
        table[position].GpsTag = FALSE;
        table[position].Tag = TagNameToValue(tag);
        exif_tag_count++;
    }
    table[position].DataLength = 0;
    table[position].Value = (char*) malloc(sizeof(char) * (value_length + 1));
    if (table[position].Value) {
        memcpy(table[position].Value, value, value_length + 1);
        table[position].DataLength = value_length + 1;
    }
    position++;
    return ret;
}
void  ExifElementsTable::saveJpeg(unsigned char* jpeg, size_t jpeg_size) {
	int ret;
	if (jpeg_opened) {
       ret = WriteJpegToBuffer(jpeg, jpeg_size);
	   ALOGD("saveJpeg :: ret =%d",ret);
       DiscardData();
       jpeg_opened = false;
    }
}
void ExifElementsTable::insertExifToJpeg(unsigned char* jpeg, size_t jpeg_size) {
    ReadMode_t read_mode = (ReadMode_t)(READ_METADATA | READ_IMAGE);
    ResetJpgfile();
    if (ReadJpegSectionsFromBuffer(jpeg, jpeg_size, read_mode)) {
        jpeg_opened = true;
        create_EXIF(table, exif_tag_count, gps_tag_count,true);
    }
}
status_t ExifElementsTable::insertExifThumbnailImage(const char* thumb, int len) {
    status_t ret = NO_ERROR;
    if ((len > 0) && jpeg_opened) {
        ret = ReplaceThumbnailFromBuffer(thumb, len);
        CAMHAL_LOGDB("insertExifThumbnailImage. ReplaceThumbnail(). ret=%d", ret);
    }
    return ret;
}
ExifElementsTable::~ExifElementsTable() {
    int num_elements = gps_tag_count + exif_tag_count;
    for (int i = 0; i < num_elements; i++) {
        if (table[i].Value) {
            free(table[i].Value);
        }
    }
    if (jpeg_opened) {
        DiscardData();
    }
}
} // namespace android
