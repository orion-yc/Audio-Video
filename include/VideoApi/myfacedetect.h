#ifndef MYFACEDETECT_H
#define MYFACEDETECT_H

#include "video_common.h"

class MyFaceDetect
{
public:
    MyFaceDetect();
public:
    // 0.人脸识别的初始化
    static void FaceDetectInit();
    // 1.获取摄像头图片后 识别出人脸的位置, 返回位置对应的矩形框
    static void detectAndDisplay(cv::Mat &frame , std::vector<cv::Rect> &faces);

};

#endif // MYFACEDETECT_H
