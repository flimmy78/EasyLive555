#include "LiveStreamMediaSource.hh"
#include "MPEGVideoStreamParser.hh"
#include "debuginfo.h"

callback_func LiveStreamMediaSource::funDemandIDR = NULL;
void * LiveStreamMediaSource::callback_ptr = NULL;

callback_func LiveStreamMediaSource::funDemandStreamStart = NULL;
void * LiveStreamMediaSource::callback_ptr_stram = NULL;

bool LiveStreamMediaSource::m_bVideoStart = false;
FramedSource* LiveStreamMediaSource::fOurVideoSource = NULL;
FramedSource* LiveStreamMediaSource::fOurAudioSource = NULL;

extern pthread_mutex_t m_bVideoStartRW;

static int PopFrontErrCount = 0;
static int g_fwdFrameDataLen = 0;

FILE* fp_acc;
//**********************************************************************//
FILE *fp_h265 = NULL;
int read_h265(unsigned char *buf, int buf_size){
	if (fp_h265 == NULL)
	{
		fp_h265 = fopen("test.265", "rb");
	}

	if (!feof(fp_h265)){
		int true_size = fread(buf, 1, buf_size, fp_h265);
		return true_size;
	}
	else{
		fseek(fp_h265, 0L, SEEK_SET);
		return 0;
	}
}
//**********************************************************************//

LiveStreamMediaSource* LiveStreamMediaSource::createNew(UsageEnvironment& env)
{
	return new LiveStreamMediaSource(env);
}

LiveStreamMediaSource::LiveStreamMediaSource(UsageEnvironment& env)
: Medium(env)
{
	m_H265VideoSrc = new CCircularQueue();
}

LiveStreamMediaSource::~LiveStreamMediaSource()
{
	DEBUG_ERROR("~LiveStreamMediaSource::~LiveStreamMediaSource()");
	if (fOurVideoSource != NULL)
	{
		DEBUG_ERROR("LiveStreamMediaSource::~LiveStreamMediaSource");
		Medium::close(fOurVideoSource);
		fOurVideoSource = NULL;
	}

	if (m_H265VideoSrc != NULL)
	{
		delete[] m_H265VideoSrc;
		m_H265VideoSrc = NULL;
	}
}


void LiveStreamMediaSource::VideoPointerSync()
{
	if (funDemandIDR != NULL)//pointer not null
	{
		funDemandIDR(callback_ptr, 0);
	}
}

void LiveStreamMediaSource::SetVideoPointerSyncCallback(callback_func handler, void *ptr)
{
	funDemandIDR = handler;
	callback_ptr = ptr;
}

void LiveStreamMediaSource::SetVideoDemandStreamStartCallback(callback_func handler, void *ptr)
{
	funDemandStreamStart = handler;
	callback_ptr_stram = ptr;
}


FramedSource* LiveStreamMediaSource::videoSource()
{
	DEBUG_ERROR("=======================>>>>>get videoSource m_bVideoStart:%d<<<==================", m_bVideoStart);
	//	if(!m_bVideoStart)
	//	{
	//		return NULL;
	//	}

	//DEMAND IDR
	//CCircularQueue::getInstance()->SyncRwPoint();
	m_H265VideoSrc->SyncRwPoint();
	VideoPointerSync();

	if (fOurVideoSource == NULL)
	{
		DEBUG_ERROR("fOurVideoSource == NULL");
		fOurVideoSource = VideoOpenSource::createNew(envir(), NULL, *this);

		DEBUG_ERROR("===fOurVideoSource:%x====", fOurVideoSource);
	}

	return fOurVideoSource;
}

FramedSource* LiveStreamMediaSource::audioSource()
{
	if (fOurAudioSource == NULL)
	{
		DEBUG_ERROR("fOurVideoSource == NULL");
		fOurAudioSource = AudioOpenSource::createNew(envir(), NULL, *this);

		DEBUG_ERROR("===fOurAudioSource:%x====", fOurAudioSource);
	}

	return fOurAudioSource;
}


VideoOpenSource*
VideoOpenSource::createNew(UsageEnvironment& env, char const* fileName, LiveStreamMediaSource& input,
unsigned preferredFrameSize,
unsigned playTimePerFrame) {

	VideoOpenSource* newSource = new VideoOpenSource(env, input, preferredFrameSize, playTimePerFrame);

	return newSource;
}

void VideoOpenSource::SyncWRPointer()
{
	//CCircularQueue::getInstance()->SyncRwPoint();
	fInput.m_H265VideoSrc->SyncRwPoint();
}


VideoOpenSource::VideoOpenSource(UsageEnvironment& env,
	LiveStreamMediaSource& input,
	unsigned preferredFrameSize,
	unsigned playTimePerFrame)
	: FramedSource(env), fInput(input), fPreferredFrameSize(preferredFrameSize),
	fPlayTimePerFrame(playTimePerFrame), fLastPlayTime(0),
	fHaveStartedReading(False), fLimitNumBytesToStream(False), fNumBytesToStream(0)
	, m_FrameRate(DEFAULT_MAX_VEDIOFRAME_RATE){

	uSecsToDelay = 1000;
	uSecsToDelayMax = 1666;
}

VideoOpenSource::~VideoOpenSource()
{
	DEBUG_ERROR("VideoOpenSource::~VideoOpenSource()");
	if (fInput.fOurVideoSource != NULL)
	{
		Medium::close(fInput.fOurVideoSource);
		fInput.fOurVideoSource = NULL;
	}
}

void VideoOpenSource::doGetNextFrame() {
	//do read from memory
	incomingDataHandler(this);
}
void VideoOpenSource::incomingDataHandler(VideoOpenSource* source) {
	source->incomingDataHandler1();
}


void VideoOpenSource::incomingDataHandler1()
{
	if (fLimitNumBytesToStream && fNumBytesToStream < (u_int64_t)fMaxSize) {
		fMaxSize = (unsigned)fNumBytesToStream;
	}
	if (fPreferredFrameSize > 0 && fPreferredFrameSize < fMaxSize) {
		fMaxSize = fPreferredFrameSize;
	}

	fFrameSize = 0;
	Queue_Element elem;
	int unHandleCnt = 0;

	//add for file simulation test
	//if(CCircularQueue::getInstance()->Size() < 2)
	if (fInput.m_H265VideoSrc->Size() < 2)
	{
		//read file , push 
		Queue_Element pushElem;
		int rdSize = read_h265(pushElem.data, 64 * 1024);
		pushElem.lenght = rdSize;
		pushElem.FrameCompleted = 1;
		//CCircularQueue::getInstance()->PushBack(pushElem);
		fInput.m_H265VideoSrc->PushBack(pushElem);
	}

	//if(CCircularQueue::getInstance()->PopFront(&elem,unHandleCnt))
	if (fInput.m_H265VideoSrc->PopFront(&elem, unHandleCnt))
	{
		//DEBUG_DEBUG("====>>>data:%x elem.lenght:%d",elem.data,elem.lenght);
		fFrameSize = elem.lenght;
		PopFrontErrCount = 0;
	}
	else
	{
		PopFrontErrCount++;
		if (PopFrontErrCount > 500)
		{
			DEBUG_DEBUG("Error ====>>>data:%x elem.lenght:%d", elem.data, elem.lenght);
			PopFrontErrCount = 0;
		}
	}

	if (fFrameSize < 0)
	{
		handleClosure(this);
		return;
	}
	else if (fFrameSize == 0)
	{
		if (uSecsToDelay >= uSecsToDelayMax)
		{
			uSecsToDelay = uSecsToDelayMax;
		}
		else{
			uSecsToDelay *= 2;
		}

		if (PopFrontErrCount % 10 == 0)
		{
			fNumTruncatedBytes = 0;
			gettimeofday(&fPresentationTime, NULL);
			//DEBUG_ERROR("No data for long time, call FramedSource::afterGetting");
			nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				(TaskFunc*)FramedSource::afterGetting, this);
		}
		else
		{
			nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecsToDelay,
				(TaskFunc*)incomingDataHandler, this);
		}
	}
	else
	{
		if (fFrameSize + g_fwdFrameDataLen > fMaxSize)
		{
			DEBUG_ERROR("----->>>>fFrameSize > fMaxSize lost data!!!");
			fNumTruncatedBytes = fFrameSize + g_fwdFrameDataLen - fMaxSize;
			fFrameSize = fMaxSize - g_fwdFrameDataLen;
		}
		else
		{
			fNumTruncatedBytes = 0;
		}


		if (elem.FrameCompleted == 0)
		{
			memcpy(fTo, elem.data, fFrameSize);
			g_fwdFrameDataLen += fFrameSize;

			nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecsToDelay,
				(TaskFunc*)incomingDataHandler, this);
		}
		else
		{
			memcpy(fTo + g_fwdFrameDataLen, elem.data, fFrameSize);
			g_fwdFrameDataLen = 0;
			gettimeofday(&fPresentationTime, NULL);

			nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
				(TaskFunc*)FramedSource::afterGetting, this);
		}
	}
}



//===============================================================================================================

AudioOpenSource*
AudioOpenSource::createNew(UsageEnvironment& env, char const* fileName, LiveStreamMediaSource& input,
unsigned preferredFrameSize,
unsigned playTimePerFrame) {

	AudioOpenSource* newSource = new AudioOpenSource(env, input, preferredFrameSize, playTimePerFrame);

	return newSource;
}

AudioOpenSource::AudioOpenSource(UsageEnvironment& env,
	LiveStreamMediaSource& input,
	unsigned preferredFrameSize,
	unsigned playTimePerFrame)
	: FramedSource(env), fInput(input){

  fSamplingFrequency = 44100;
  fNumChannels = 2;
  fuSecsPerFrame
    = (1024/*samples-per-frame*/*1000000) / fSamplingFrequency/*samples-per-second*/;

  fp_acc = fopen("test.aac", "rb");
}

AudioOpenSource::~AudioOpenSource()
{
	DEBUG_ERROR("VideoOpenSource::~VideoOpenSource()");
	if (fInput.fOurAudioSource != NULL)
	{
		Medium::close(fInput.fOurAudioSource);
		fInput.fOurAudioSource = NULL;
	}
}

void AudioOpenSource::doGetNextFrame() {
	//do read from memory
	incomingDataHandler(this);
}

void AudioOpenSource::incomingDataHandler(AudioOpenSource* source) {
	source->incomingDataHandler1();
}

void AudioOpenSource::incomingDataHandler1()
{
	// Begin by reading the 7-byte fixed_variable headers:
	unsigned char headers[7];
	if (fread(headers, 1, sizeof headers, fp_acc) < sizeof headers
		|| feof(fp_acc) || ferror(fp_acc)) {
		// The input source has ended:
		handleClosure(this);
		return;
	}

	// Extract important fields from the headers:
	Boolean protection_absent = headers[1] & 0x01;
	u_int16_t frame_length
		= ((headers[3] & 0x03) << 11) | (headers[4] << 3) | ((headers[5] & 0xE0) >> 5);
#ifdef DEBUG
	u_int16_t syncword = (headers[0] << 4) | (headers[1] >> 4);
	fprintf(stderr, "Read frame: syncword 0x%x, protection_absent %d, frame_length %d\n", syncword, protection_absent, frame_length);
	if (syncword != 0xFFF) fprintf(stderr, "WARNING: Bad syncword!\n");
#endif
	unsigned numBytesToRead
		= frame_length > sizeof headers ? frame_length - sizeof headers : 0;

	// If there's a 'crc_check' field, skip it:
	if (!protection_absent) {
		fseeko(fp_acc, 2, SEEK_CUR);
		numBytesToRead = numBytesToRead > 2 ? numBytesToRead - 2 : 0;
	}

	// Next, read the raw frame data into the buffer provided:
	if (numBytesToRead > fMaxSize) {
		fNumTruncatedBytes = numBytesToRead - fMaxSize;
		numBytesToRead = fMaxSize;
	}
	int numBytesRead = fread(fTo, 1, numBytesToRead, fp_acc);
	if (numBytesRead < 0) numBytesRead = 0;
	fFrameSize = numBytesRead;
	fNumTruncatedBytes += numBytesToRead - numBytesRead;

	// Set the 'presentation time':
	if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {
		// This is the first frame, so use the current time:
		gettimeofday(&fPresentationTime, NULL);
	}
	else {
		// Increment by the play time of the previous frame:
		unsigned uSeconds = fPresentationTime.tv_usec + fuSecsPerFrame;
		fPresentationTime.tv_sec += uSeconds / 1000000;
		fPresentationTime.tv_usec = uSeconds % 1000000;
	}

	fDurationInMicroseconds = fuSecsPerFrame;

	// Switch to another task, and inform the reader that he has data:
	nextTask() = envir().taskScheduler().scheduleDelayedTask(0,
		(TaskFunc*)FramedSource::afterGetting, this);
}