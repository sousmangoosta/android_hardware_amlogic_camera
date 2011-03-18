LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcamera

LOCAL_SHARED_LIBRARIES := \
		libutils \
		libcutils \
    liblog  \
    libcamera_client \
    libbinder \
    libjpeg

ifeq ($(BOARD_HAVE_MULTI_CAMERAS),true)
	LOCAL_CFLAGS += -DAMLOGIC_MULTI_CAMERA_SUPPORT
endif
    
LOCAL_C_INCLUDES += ../../../kernel/include/

#jpeg encode
LOCAL_SRC_FILES += jpegenc/amljpeg_enc.c

#LOCAL_SRC_FILES += AmlogicCameraHardware.cpp FakeCamera/FakeCamera.cpp
LOCAL_SRC_FILES += AmlogicCameraHardware.cpp V4L2/V4L2Camera.cpp FakeCamera/FakeCamera.cpp OpCameraHardware.c




include $(BUILD_SHARED_LIBRARY)
