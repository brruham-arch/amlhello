LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE     := AMLHello
LOCAL_SRC_FILES  := main.cpp
LOCAL_LDLIBS     := -llog
LOCAL_CPPFLAGS   := -std=c++17 -O2 -fvisibility=hidden
LOCAL_CFLAGS     := -O2

# Pastikan 3 fungsi ini tidak di-strip/hidden
LOCAL_LDFLAGS    := -Wl,--export-dynamic \
                    -Wl,-e,__GetModInfo

include $(BUILD_SHARED_LIBRARY)
