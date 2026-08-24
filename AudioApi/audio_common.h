#ifndef AUDIO_COMMON_H
#define AUDIO_COMMON_H

//获取声音和播放声音的组件是 Qt 自带的 QAudioInput 和 QAudioOutput ,
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#include <QMessageBox>
#include <QDebug>

//使用speex
#include <speex/speex.h>

#define USE_SPEEX   1
//#define USE_VAD  1
//设置压缩质量
#define SPEEX_QUALITY (8)


#endif // AUDIO_COMMON_H
