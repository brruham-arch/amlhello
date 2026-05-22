LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := AMLHello
LOCAL_SRC_FILES  := main.cpp
LOCAL_LDLIBS     := -llog -lpthread
LOCAL_CPPFLAGS   := -std=c++17 -O2
LOCAL_CFLAGS     := -O2
LOCAL_LDFLAGS    := -Wl,--no-undefined

include $(BUILD_SHARED_LIBRARY)
