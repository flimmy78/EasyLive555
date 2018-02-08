#ifndef _AMBA_AUDIO_STREAM_SOURCE_H
#define _AMBA_AUDIO_STREAM_SOURCE_H

#ifndef _FRAMED_SOURCE_HH
    #include "FramedSource.hh"
#endif

#include "stream_reader.h"

class AmbaAudioStreamSource: public FramedSource
{
public:
    static AmbaAudioStreamSource *createNew(UsageEnvironment &env,
                                            stream_reader_t *pStream_reader,
                                            char const *fileName, u_int32_t StreamTag);

    static void retryLater(void *firstArg);

    AMP_FORMAT_MID_e getSourceCodecID() { return fCodec_id; }

    unsigned samplingFrequency() const { return fSamplingFrequency; }
    unsigned numChannels() const { return fNumChannels; }
    char const *configStr() const { return fConfigStr; }

    void seekStream(int32_t seekNPT);

    // returns the 'AudioSpecificConfig' for this stream (in ASCII form)

protected:
    AmbaAudioStreamSource(UsageEnvironment &env, stream_reader_t *pStream_reader,
                          char const *fileName, u_int32_t StreamTag);
    // called only by createNew()

    virtual ~AmbaAudioStreamSource();

    // redefined virtual functions:
    virtual void doGetNextFrame();
    virtual void doStopGettingFrames();

private:
    stream_type_t           fStream_type;
    stream_reader_t         *fStream_reader;
    stream_fifo_reader_t    *fFifo_reader;
    Boolean                 fStartQueueFrames;
    AMP_FORMAT_MID_e        fCodec_id;

    u_int32_t           fStreamTag;
    int32_t             fRemainLen;
    u_int8_t            *fCurAddr;
    float               fuSecsPerFrame;
    struct timeval      fAudioStartTime;

    float               fuSec_90k_converter;
    u_int64_t           fPrevFrameTime;
    int64_t             fFirstFramePTS;
    int64_t             fCurFramePTS;
    u_int64_t           fCurFramePTSInMicrosec;

    unsigned    fSamplingFrequency;
    unsigned    fNumChannels;
    char        fConfigStr[5];

    u_int8_t    fOperatingState;
    int32_t     fSeekTime;
};

#endif