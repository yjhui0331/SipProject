/*
#define PJSUA_HAS_VIDEO 1					//������Ƶ  
#define PJMEDIA_HAS_VIDEO 1					//������Ƶ
#define PJMEDIA_VIDEO_DEV_HAS_SDL       1 //����SDL��Ƶ�豸�������޷���ʾ��Ƶ  
#define PJMEDIA_VIDEO_DEV_HAS_DSHOW     1 //����direct show������DirectX��������microsip��setting�Ի����е�Camera�޷��������USB����ͷ�豸
#define PJMEDIA_HAS_FFMPEG              1 //����ffmpeg����Ҫ�õ�h264��codec������Ƶ�����
#define PJMEDIA_HAS_FFMPEG_VID_CODEC 1	  //����ffmepg codec
#define PJMEDIA_HAS_FFMPEG_CODEC_H264 1	  //����h264,�����ã�microsip��setting�Ի����е�codec������h264ѡ�
*/

#define PJSUA_HAS_VIDEO 1					//������Ƶ  
#define PJMEDIA_HAS_VIDEO 1
//#define PJMEDIA_HAS_OPENH264_CODEC 1
//#define PJMEDIA_HAS_LIBYUV 1
#define PJMEDIA_VIDEO_DEV_HAS_SDL 1
#define PJMEDIA_VIDEO_DEV_HAS_DSHOW 1
#define PJMEDIA_HAS_FFMPEG 1
#define PJMEDIA_HAS_FFMPEG_VID_CODEC 1	
#define PJMEDIA_HAS_FFMPEG_CODEC_H264 1	

//#define PJMEDIA_HAS_WEBRTC_AEC 1		//����WEBRTC
//#define PJMEDIA_WEBRTC_AEC_USE_MOBILE 1 //����WEBRTC

//#define PJ_CONFIG_MAXIMUM_SPEED 1 //�����������

//#include <pj/config_site_sample.h>