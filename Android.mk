LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcamera

LOCAL_SHARED_LIBRARIES := \
		libcutils \
    libutils \
    liblog  \
    libcamera_client \
    libbinder \
    libjpeg
    

LOCAL_SRC_FILES += AmlogicCameraHardware.cpp OpCameraHardware.c OpVdin.c cmem.c

include $(BUILD_SHARED_LIBRARY)
