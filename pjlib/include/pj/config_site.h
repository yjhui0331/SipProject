/*
#define PJSUA_HAS_VIDEO 1					//启用视频  
#define PJMEDIA_HAS_VIDEO 1					//启用视频
#define PJMEDIA_VIDEO_DEV_HAS_SDL       1 //启用SDL视频设备，否则无法显示视频  
#define PJMEDIA_VIDEO_DEV_HAS_DSHOW     1 //启用direct show，依赖DirectX，不起用microsip的setting对话框中的Camera无法看到你的USB摄像头设备
#define PJMEDIA_HAS_FFMPEG              1 //启用ffmpeg，需要用到h264的codec进行视频编解码
#define PJMEDIA_HAS_FFMPEG_VID_CODEC 1	  //启用ffmepg codec
#define PJMEDIA_HAS_FFMPEG_CODEC_H264 1	  //启用h264,不起用，microsip的setting对话框中的codec设置无h264选项。
*/

#define PJSUA_HAS_VIDEO 1					//启用视频  
#define PJMEDIA_HAS_VIDEO 1
//#define PJMEDIA_HAS_OPENH264_CODEC 1
//#define PJMEDIA_HAS_LIBYUV 1
#define PJMEDIA_VIDEO_DEV_HAS_SDL 1
#define PJMEDIA_VIDEO_DEV_HAS_DSHOW 1
#define PJMEDIA_HAS_FFMPEG 1
#define PJMEDIA_HAS_FFMPEG_VID_CODEC 1	
#define PJMEDIA_HAS_FFMPEG_CODEC_H264 1	

//#define PJMEDIA_HAS_WEBRTC_AEC 1		//启用WEBRTC
//#define PJMEDIA_WEBRTC_AEC_USE_MOBILE 1 //启用WEBRTC

//#define PJ_CONFIG_MAXIMUM_SPEED 1 //开启最佳性能

//#include <pj/config_site_sample.h>