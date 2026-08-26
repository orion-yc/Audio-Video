HEADERS += \
    $$PWD/desk_read.h \
    $$PWD/h264codec.h \
    $$PWD/myfacedetect.h \
    $$PWD/video_common.h \
    $$PWD/video_encode_thread.h \
    $$PWD/video_read.h

SOURCES += \
    $$PWD/desk_read.cpp \
    $$PWD/h264codec.cpp \
    $$PWD/myfacedetect.cpp \
    $$PWD/video_encode_thread.cpp \
    $$PWD/video_read.cpp

msvc {
    VCPKG_ROOT = $$PWD/../third_party/vcpkg/installed/x64-windows
    INCLUDEPATH += $$VCPKG_ROOT/include/opencv4

    OPENCV_WORLD_LIB = $$files($$VCPKG_ROOT/lib/opencv_world*.lib)
    isEmpty(OPENCV_WORLD_LIB) {
        error("MSVC OpenCV world library was not found. Run packaging/build-release-msvc.ps1 first.")
    }
    LIBS += $$OPENCV_WORLD_LIB
    FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
    INCLUDEPATH += $$FFMPEG_ROOT/include
    LIBS += $$FFMPEG_ROOT/lib/avcodec.lib \
        $$FFMPEG_ROOT/lib/avutil.lib \
        $$FFMPEG_ROOT/lib/swscale.lib
} else {
    OPENCV_ROOT = E:/perject/OpenCV-4.13.0-MinGW64/install
    OPENCV_LIBDIR = $$OPENCV_ROOT/x64/mingw/lib
    INCLUDEPATH += $$OPENCV_ROOT/include

    LIBS += -L$$OPENCV_LIBDIR \
        -lopencv_calib3d4130 \
        -lopencv_features2d4130 \
        -lopencv_flann4130 \
        -lopencv_highgui4130 \
        -lopencv_imgcodecs4130 \
        -lopencv_imgproc4130 \
        -lopencv_ml4130 \
        -lopencv_objdetect4130 \
        -lopencv_photo4130 \
        -lopencv_video4130 \
        -lopencv_videoio4130 \
        -lopencv_core4130

    FFMPEG_ROOT = $$PWD/../third_party/ffmpeg-master-latest-win64-lgpl-shared
    INCLUDEPATH += $$FFMPEG_ROOT/include
    LIBS += -L$$FFMPEG_ROOT/lib \
        -lavcodec \
        -lavutil \
        -lswscale
}
