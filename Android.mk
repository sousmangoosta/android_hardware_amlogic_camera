LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcamera
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := \
	libutils \
	libcutils \
	liblog  \
	libcamera_client \
	libbinder \
	libjpeg \
	libexif

ifeq ($(BOARD_HAVE_MULTI_CAMERAS),true)
	LOCAL_CFLAGS += -DAMLOGIC_MULTI_CAMERA_SUPPORT
endif
    
LOCAL_C_INCLUDES += $ANDROID_BUILD_TOP/kernel/include/

#USE V4L2 Camera 
LOCAL_SRC_FILES += jpegenc/amljpeg_enc.c
LOCAL_SRC_FILES += AmlogicCameraHardware.cpp V4L2/V4L2Camera.cpp V4L2/CameraSetting.cpp

ifeq ($(BUILD_CUSTOMIZE_CAMERA_SETTING),true)
LOCAL_CFLAGS += -DUSE_CUSTOMIZE_CAMERA_SETTING
LOCAL_STATIC_LIBRARIES := libcamera_customize
endif

#USE FAKECAMERA
#LOCAL_SRC_FILES += AmlogicCameraHardware.cpp FakeCamera/FakeCamera.cpp

include $(BUILD_SHARED_LIBRARY)
