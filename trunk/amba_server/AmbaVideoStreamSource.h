
#ifndef _AMBA_VIDEO_STREAM_SOURCE_H
#define _AMBA_VIDEO_STREAM_SOURCE_H

#ifndef _FRAMED_SOURCE_HH
    #include "FramedSource.hh"
#endif

#include "stream_reader.h"

class AmbaVideoStreamSource: public FramedSource
{
public:
    static AmbaVideoStreamSource *createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                            char const *fileName, u_int32_t StreamTag);

    // for H264 video
    int getSpsPps(u_int8_t **ppSPS_payload, u_int32_t *pSPS_length,
                  u_int8_t **ppPPS_payload, u_int32_t *pPPS_length);

    virtual void doGetNextFrame();

    static void retryLater(void *firstArg);

    virtual unsigned maxFrameSize() const;

    void seekStream(int32_t seekNPT);
    AMP_FORMAT_MID_e getSourceCodecID() { return fCodec_id; }

protected:

    AmbaVideoStreamSource(UsageEnvironment &env, stream_reader_t *pStream_reader,
                          char const *fileName, u_int32_t StreamTag); // abstract base class
    virtual ~AmbaVideoStreamSource();
    // redefined virtual functions:
    virtual void doStopGettingFrames();

    virtual Boolean isH264VideoStreamFramer() const;
    virtual Boolean isH265VideoStreamFramer() const;

private:
    stream_type_t           fStream_type;
    stream_reader_t         *fStream_reader;
    stream_fifo_reader_t    *fFifo_reader;
    Boolean                 fStartQueueFrames;
    float                   fuSec_90k_converter;
    AMP_FORMAT_MID_e        fCodec_id;

    u_int32_t           fStreamTag;
    int32_t             fRemainLen;
    u_int8_t            *fCurParseAddr;

    struct timeval      fVideoStartTime;
    u_int64_t           fCurFramePTSInMicrosec;

    u_int64_t           fPrevFrameTime;
    int64_t             fFirstFramePTS;
    int64_t             fCurFramePTS;

    u_int32_t           fuSecsPerFrame;

    u_int8_t            fOperatingState;
    int32_t             fSeekTime; // ms
};

#endif
