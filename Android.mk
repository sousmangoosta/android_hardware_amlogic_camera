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

ifeq ($(BOARD_HAVE_FRONT_CAM),true)
	LOCAL_CFLAGS += -DAMLOGIC_FRONT_CAMERA_SUPPORT
endif

ifeq ($(BOARD_HAVE_BACK_CAM),true)
	LOCAL_CFLAGS += -DAMLOGIC_BACK_CAMERA_SUPPORT
endif

ifeq ($(BOARD_USE_USB_CAMERA),true)
	LOCAL_CFLAGS += -DAMLOGIC_USB_CAMERA_SUPPORT
endif

ifeq ($(BOARD_HAVE_FLASHLIGHT),true)
	LOCAL_CFLAGS += -DAMLOGIC_FLASHLIGHT_SUPPORT
endif

#LOCAL_C_INCLUDES += $ANDROID_BUILD_TOP/kernel/include/

#USE V4L2 Camera 
LOCAL_SRC_FILES += jpegenc/amljpeg_enc.c
LOCAL_SRC_FILES += AmlogicCameraHardware.cpp V4L2/V4L2Camera.cpp V4L2/CameraSetting.cpp V4L2/OpCameraHardware.c

#USE FAKECAMERA
#LOCAL_SRC_FILES += AmlogicCameraHardware.cpp FakeCamera/FakeCamera.cpp

include $(BUILD_SHARED_LIBRARY)
