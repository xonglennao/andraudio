
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := proxytest
LOCAL_SRC_FILES  := test.cpp
LOCAL_C_INCLUDES := $(ANDROID_SRC)/frameworks/base/include \
                    $(ANDROID_SRC)/system/core/include \
                    $(ANDROID_SRC)/hardware/libhardware_legacy/include \
                    $(ANDROID_SRC)/hardware
LOCAL_LDLIBS     := -L$(LOCAL_PATH)/../system_libs -lutils -lcutils -laudio
LOCAL_CXXFLAGS   := -Wall -D__STDC_LIMIT_MACROS

ifeq (x$(TARGET), xmsm7k)
LOCAL_CXXFLAGS += -DTARGET_MSM7K
endif

include $(BUILD_EXECUTABLE)
