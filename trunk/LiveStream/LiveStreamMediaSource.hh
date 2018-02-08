#ifndef _LIVE_STREAM_MEDIA_SOURCE_HH
#define _LIVE_STREAM_MEDIA_SOURCE_HH

#include <MediaSink.hh>
#include "CircularQueue.h"

typedef void (*callback_func)(void *ptr,int streamid);
#define DEFAULT_MAX_VEDIOFRAME_RATE 30
#define DEFAULT_MAX_VEDIOFRAME_SIZE 64 * 1024

class LiveStreamMediaSource:public Medium
{
public:
	
	~LiveStreamMediaSource();
	static LiveStreamMediaSource* createNew(UsageEnvironment& env);

	FramedSource* videoSource();
	FramedSource* audioSource();
	void VideoPointerSync();	//sync read && write pointer and demand IDR
	static void SetVideoPointerSyncCallback(callback_func handler,void *ptr);
	static void SetVideoDemandStreamStartCallback(callback_func handler,void *ptr);
	
private:
	LiveStreamMediaSource(UsageEnvironment& env);
public:
	static callback_func funDemandIDR;
	static void * callback_ptr;
	
    static callback_func funDemandStreamStart;
    static void * callback_ptr_stram;
	static bool    m_bVideoStart;
	static FramedSource* fOurVideoSource;
	static FramedSource* fOurAudioSource;

	CCircularQueue* m_H265VideoSrc;
};


#ifndef _FRAMED_FILE_SOURCE_HH
#include "FramedSource.hh"
#endif

class VideoOpenSource: public FramedSource
{ 
public:
  static VideoOpenSource* createNew(UsageEnvironment& env,
					 char const* fileName,
					 LiveStreamMediaSource& input,
					 unsigned preferredFrameSize = 0,
					 unsigned playTimePerFrame = 0);
  // "preferredFrameSize" == 0 means 'no preference'
  // "playTimePerFrame" is in microseconds

  unsigned maxFrameSize() const {
    return DEFAULT_MAX_VEDIOFRAME_SIZE;
  }

  void SyncWRPointer();

protected:
  VideoOpenSource(UsageEnvironment& env,
		       LiveStreamMediaSource& input,
		       unsigned preferredFrameSize,
		       unsigned playTimePerFrame);

  virtual ~VideoOpenSource();

  static void incomingDataHandler(VideoOpenSource* source);
  void incomingDataHandler1();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

protected:
  LiveStreamMediaSource& fInput;

private:
  unsigned fPreferredFrameSize;
  unsigned fPlayTimePerFrame;
  Boolean fFidIsSeekable;
  unsigned fLastPlayTime;
  Boolean fHaveStartedReading;
  Boolean fLimitNumBytesToStream;
  u_int64_t fNumBytesToStream; // used iff "fLimitNumBytesToStream" is True

  double m_FrameRate;
  int  uSecsToDelay;
  int  uSecsToDelayMax;
};


class AudioOpenSource: public FramedSource
{
public:
  static AudioOpenSource* createNew(UsageEnvironment& env,
					 char const* fileName,
					 LiveStreamMediaSource& input,
					 unsigned preferredFrameSize = 0,
					 unsigned playTimePerFrame = 0);

protected:
    AudioOpenSource(UsageEnvironment& env,
                    LiveStreamMediaSource& input,
                    unsigned preferredFrameSize,
                    unsigned playTimePerFrame);

    virtual ~AudioOpenSource();

    static void incomingDataHandler(AudioOpenSource* source);
    void incomingDataHandler1();
private:
  // redefined virtual functions:
    virtual void doGetNextFrame();
  
protected:
    LiveStreamMediaSource& fInput;

    unsigned fSamplingFrequency;
    unsigned fNumChannels;
    unsigned fuSecsPerFrame;
};

#endif
