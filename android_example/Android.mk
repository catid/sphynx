
LOCAL_PATH:= $(call my-dir)

#--------------------------------------------------------
# libvrcubeworld.so
#--------------------------------------------------------
include $(CLEAR_VARS)

PROJECT_FILES := $(wildcard $(LOCAL_PATH)/../../../Src/*.cpp)
PROJECT_FILES += $(wildcard $(LOCAL_PATH)/../../../Src/sphynx/*.cpp)
PROJECT_FILES += $(wildcard $(LOCAL_PATH)/../../../Src/sphynx/g3log/*.cpp)
PROJECT_FILES += $(wildcard $(LOCAL_PATH)/../../../Src/sphynx/zstd/*.c)
PROJECT_FILES := $(filter-out $(LOCAL_PATH)/../../../Src/sphynx/g3log/crashhandler_windows.cpp, $(PROJECT_FILES))
PROJECT_FILES := $(filter-out $(LOCAL_PATH)/../../../Src/sphynx/g3log/stacktrace_windows.cpp, $(PROJECT_FILES))
PROJECT_FILES := $(PROJECT_FILES:$(LOCAL_PATH)/%=%)

LOCAL_MODULE			:= vrcubeworld
LOCAL_C_INCLUDES        := $(LOCAL_PATH)/../../../Src/sphynx
LOCAL_CFLAGS			:= -Werror
LOCAL_CPPFLAGS			:= -std=c++14 -Wno-narrowing -fexceptions -frtti
LOCAL_CFLAGS            += -DDISABLE_FATAL_SIGNALHANDLING=1
LOCAL_CFLAGS            += -DDISABLE_VECTORED_EXCEPTIONHANDLING=1
LOCAL_CFLAGS            += -DASIO_STANDALONE=1
LOCAL_CFLAGS            += -DASIO_NO_DEPRECATED=1
LOCAL_CFLAGS            += -DASIO_HEADER_ONLY=1
LOCAL_SRC_FILES			:= $(PROJECT_FILES)
LOCAL_LDLIBS			:= -llog -landroid -lGLESv3 -lEGL		# include default libraries

LOCAL_STATIC_LIBRARIES	:= systemutils android_native_app_glue 
LOCAL_SHARED_LIBRARIES	:= vrapi

include $(BUILD_SHARED_LIBRARY)

$(call import-module,android/native_app_glue)
$(call import-module,VrApi/Projects/AndroidPrebuilt/jni)
$(call import-module,VrAppSupport/SystemUtils/Projects/AndroidPrebuilt/jni)
