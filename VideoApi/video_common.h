#ifndef VIDEO_COMMON_H
#define VIDEO_COMMON_H
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <QObject>

using namespace cv;

// H.264 target rates for a 200 Mbps connection with a small meeting room.
#define CAMERA_FRAME_RATE (60)
#define CAMERA_IMAGE_WIDTH (1920)
#define CAMERA_IMAGE_HEIGHT (1080)
#define CAMERA_VIDEO_BITRATE (8 * 1000 * 1000)

// Desktop sharing defaults to 1440p. The room controls can raise it to 4K.
#define DESKTOP_FRAME_RATE (60)
#define DESKTOP_IMAGE_WIDTH (2560)
#define DESKTOP_IMAGE_HEIGHT (1440)
#define DESKTOP_VIDEO_BITRATE (32 * 1000 * 1000)
#define DESKTOP_MAX_FRAME_RATE (60)
#define DESKTOP_MAX_IMAGE_WIDTH (3840)
#define DESKTOP_MAX_IMAGE_HEIGHT (2160)
#define DESKTOP_MAX_VIDEO_BITRATE (56 * 1000 * 1000)
#endif // VIDEO_COMMON_H
