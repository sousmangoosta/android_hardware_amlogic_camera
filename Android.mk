ifneq ($(strip $(USE_CAMERA_STUB)),true)

LOCAL_PATH:= $(call my-dir)

CAMERA_HAL_SRC := \
	CameraHal_Module.cpp \
	CameraHal.cpp \
	CameraHalUtilClasses.cpp \
	AppCallbackNotifier.cpp \
	ANativeWindowDisplayAdapter.cpp \
	CameraProperties.cpp \
	MemoryManager.cpp \
	Encoder_libjpeg.cpp \
	SensorListener.cpp  \
	NV12_resize.c

CAMERA_COMMON_SRC:= \
	CameraParameters.cpp \
	TICameraParameters.cpp \
	CameraHalCommon.cpp

CAMERA_V4L_SRC:= \
	BaseCameraAdapter.cpp \
	V4LCameraAdapter/V4LCameraAdapter.cpp

CAMERA_UTILS_SRC:= \
	utils/ErrorUtils.cpp \
	utils/MessageQueue.cpp \
	utils/Semaphore.cpp


include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	$(CAMERA_HAL_SRC) \
	$(CAMERA_V4L_SRC) \
	$(CAMERA_COMMON_SRC) \
	$(CAMERA_UTILS_SRC)

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/inc/ \
    $(LOCAL_PATH)/utils \
    $(LOCAL_PATH)/inc/V4LCameraAdapter \
    frameworks/base/include/ui \
    frameworks/base/include/utils \
    external/jhead/ \
    external/jpeg/ \
    hardware/libhardware/modules/gralloc/

    #$(LOCAL_PATH)/../include \
    #$(LOCAL_PATH)/../hwc \
    #hardware/ti/omap4xxx/tiler \
    #hardware/ti/omap4xxx/ion \
    #frameworks/base/include/media/stagefright/openmax

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libbinder \
    libutils \
    libcutils \
    libcamera_client \
    libexif \
    libjpeg \
    libgui

    #libtiutils \
    #libion \

LOCAL_CFLAGS := -fno-short-enums -DCOPY_IMAGE_BUFFER
LOCAL_CFLAGS += -DLOG_NDEBUG=0

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.amlogic
LOCAL_MODULE_TAGS:= optional

#include $(BUILD_HEAPTRACKED_SHARED_LIBRARY)
include $(BUILD_SHARED_LIBRARY)
endif
