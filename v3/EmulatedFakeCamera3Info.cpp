/*
 * Copyright (C) 2014 The Android Open Source Project
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

/*
 * Contains implementation of a class EmulatedFakeCamera3 that encapsulates
 * functionality of an advanced fake camera.
 */


#include <camera/CameraMetadata.h>
#include "EmulatedFakeCamera3.h"
#include "inc/DebugUtils.h"
#define LOG_TAG "EmulatedCamera_FakeCamera3Info"

namespace android {

//level: legacy:0-4, limited:1-5, full:2-6
const struct EmulatedFakeCamera3::KeyInfo_s EmulatedFakeCamera3::sKeyInfo[] = {
        {ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES, 0,},
        {ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, 0,},
        {ANDROID_CONTROL_AE_AVAILABLE_MODES, 0,},
        {ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, 0,},
        {ANDROID_CONTROL_AE_COMPENSATION_RANGE, 0,},
        {ANDROID_CONTROL_AE_COMPENSATION_STEP, 0,},
        {ANDROID_CONTROL_AF_AVAILABLE_MODES, 0,},
        {ANDROID_CONTROL_AVAILABLE_EFFECTS, 0,},
        {ANDROID_CONTROL_AVAILABLE_SCENE_MODES, 0,},
        {ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES, 0,},
        {ANDROID_CONTROL_AWB_AVAILABLE_MODES, 0,},
        //{ANDROID_CONTROL_MAX_REGIONS, 0,},
        {ANDROID_EDGE_AVAILABLE_EDGE_MODES, 2,},
        {ANDROID_FLASH_INFO_AVAILABLE, 0,},
        {ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, 0,},
        {ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES, 0,},
        {ANDROID_LENS_FACING, 0,},
        {ANDROID_LENS_INFO_AVAILABLE_APERTURES, 2,},
        {ANDROID_LENS_INFO_AVAILABLE_FILTER_DENSITIES, 2,},
        {ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS, 0,},
        {ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION, 1,},
        {ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION, 1,},
        {ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, 1,},
        {ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, 1,},
        {ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES, 0,},
        {ANDROID_REQUEST_AVAILABLE_CAPABILITIES, 0,},
        {ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, 0,},
        //{ANDROID_REQUEST_MAX_NUM_OUTPUT_PROC, 0,},
        //{ANDROID_REQUEST_MAX_NUM_OUTPUT_PROC_STALLING, 0,},
        //{ANDROID_REQUEST_MAX_NUM_OUTPUT_RAW, 0,},
        {ANDROID_REQUEST_PARTIAL_RESULT_COUNT, 0,},
        {ANDROID_REQUEST_PIPELINE_MAX_DEPTH, 0,},
        {ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, 0,},
        //{ANDROID_SCALER_STREAM_CONFIGURATION_MAP, 0,},
        {ANDROID_SCALER_CROPPING_TYPE, 0,},
        {ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, 0,},
        {ANDROID_SENSOR_BLACK_LEVEL_PATTERN, 2,},
        {ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, 0,},
        {ANDROID_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT, 2,},
        {ANDROID_SENSOR_INFO_EXPOSURE_TIME_RANGE, 2,},
        {ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, 2,},
        {ANDROID_SENSOR_INFO_PHYSICAL_SIZE, 0,},
        {ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, 0,},
        {ANDROID_SENSOR_INFO_SENSITIVITY_RANGE, 2,},
        {ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, 0,},
        {ANDROID_SENSOR_MAX_ANALOG_SENSITIVITY, 2,},
        {ANDROID_SENSOR_ORIENTATION, 0,},
        {ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, 0,},
        {ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, 0,},
        {ANDROID_SYNC_MAX_LATENCY, 0,},
        {ANDROID_TONEMAP_AVAILABLE_TONE_MAP_MODES, 2,},
        {ANDROID_TONEMAP_MAX_CURVE_POINTS, 2,},

        //{ANDROID_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES, 2147483647,},
        //{ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES, 2147483647,},
        //{ANDROID_SENSOR_REFERENCE_ILLUMINANT1, 2147483647,},
        //{ANDROID_SENSOR_REFERENCE_ILLUMINANT2, 2147483647,},
        //{ANDROID_SENSOR_CALIBRATION_TRANSFORM1, 2147483647,},
        //{ANDROID_SENSOR_CALIBRATION_TRANSFORM2, 2147483647,},
        //{ANDROID_SENSOR_COLOR_TRANSFORM1, 2147483647,},
        //{ANDROID_SENSOR_COLOR_TRANSFORM2, 2147483647,},
        //{ANDROID_SENSOR_FORWARD_MATRIX1, 2147483647,},
        //{ANDROID_SENSOR_FORWARD_MATRIX2, 2147483647,},
        //{ANDROID_SENSOR_INFO_WHITE_LEVEL, 2147483647,},
};

const struct EmulatedFakeCamera3::KeyInfo_s EmulatedFakeCamera3::sKeyBackwardCompat[] = {
    {ANDROID_CONTROL_AE_ANTIBANDING_MODE, 0,},
    {ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, 0,},
    {ANDROID_CONTROL_AE_LOCK, 0,},
    {ANDROID_CONTROL_AE_MODE, 0,},
    {ANDROID_CONTROL_AE_TARGET_FPS_RANGE, 0,},
    {ANDROID_CONTROL_AF_MODE, 0,},
    {ANDROID_CONTROL_AF_TRIGGER, 0,},
    {ANDROID_CONTROL_AWB_LOCK, 0,},
    {ANDROID_CONTROL_AWB_MODE, 0,},
    {ANDROID_CONTROL_CAPTURE_INTENT, 0,},
    {ANDROID_CONTROL_EFFECT_MODE, 0,},
    {ANDROID_CONTROL_MODE, 0,},
    {ANDROID_CONTROL_SCENE_MODE, 0,},
    {ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, 0,},
    {ANDROID_FLASH_MODE, 0,},
    //{ANDROID_JPEG_GPS_LOCATION, 0},
    {ANDROID_JPEG_ORIENTATION, 0,},
    {ANDROID_JPEG_QUALITY, 0,},
    {ANDROID_JPEG_THUMBNAIL_QUALITY, 0,},
    {ANDROID_JPEG_THUMBNAIL_SIZE, 0,},
    {ANDROID_SCALER_CROP_REGION, 0,},
    {ANDROID_STATISTICS_FACE_DETECT_MODE, 0,},
};

int EmulatedFakeCamera3::getAvailableChKeys(CameraMetadata *info, uint8_t level){
//level: legacy:0, limited:1, full:2
    int transLevel;
    int16_t size, sizeofbckComp;
    int i;
    int availCount = 0;
    const struct KeyInfo_s *keyInfo = &EmulatedFakeCamera3::sKeyInfo[0];

    size = sizeof(sKeyInfo)/sizeof(struct KeyInfo_s);
    sizeofbckComp = sizeof(sKeyBackwardCompat)/sizeof(sKeyBackwardCompat[0]);
    int32_t available_keys[size+sizeofbckComp];

    switch (level) {
        case ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED:
            transLevel = 1;
            break;

        case ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_FULL:
            transLevel = 2;
            break;

        default:
            CAMHAL_LOGDA("uncertain hardware level\n");
        case ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY:
            transLevel = 0;
            break;
    }

    for(i = 0; i < size ; i++){
        if (keyInfo->level <= transLevel) {
            available_keys[availCount] = keyInfo->key;
            availCount ++;
        }
        keyInfo ++;
    }

    info->update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
            (int32_t *)available_keys, availCount);
    info->update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
            (int32_t *)available_keys, availCount);

    CAMHAL_LOGVB("availableKeySize=%d\n", availCount);
    camera_metadata_entry e;
    e = info->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
    if ((e.count > 0) &&
        (ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE == e.data.u8[0])) {

        keyInfo = &EmulatedFakeCamera3::sKeyBackwardCompat[0];
        for (i = 0; i < sizeofbckComp; i ++){
            available_keys[availCount] = keyInfo->key;
            availCount ++;
            keyInfo ++;
        }
    }

    info->update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
            (int32_t *)available_keys, availCount);
    CAMHAL_LOGVB("availableKeySize=%d\n", availCount);
    return 0;
}

} //namespace android
