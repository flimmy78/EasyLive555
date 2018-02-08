
#ifndef _AMBA_VIDEO_SERVER_MEDIA_SUBSESSION_H
#define _AMBA_VIDEO_SERVER_MEDIA_SUBSESSION_H

#ifndef _FILE_SERVER_MEDIA_SUBSESSION_HH
    #include "FileServerMediaSubsession.hh"
#endif

#ifndef _H264_VIDEO_RTP_SINK_HH
    #include "H264VideoRTPSink.hh"
#endif

#ifndef _RTCP_HH
    #include "RTCP.hh"
#endif

#include "stream_reader.h"
//=============================================================================
//                  Class Definition
//=============================================================================
class AmbaVideoServerMediaSubsession: public FileServerMediaSubsession
{
public:
    static AmbaVideoServerMediaSubsession *
    createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
              char const *fileName, Boolean reuseFirstSource);

    virtual float duration() const;

    static void rtcpRRQos(void* clientData);

protected:
    AmbaVideoServerMediaSubsession(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                   char const *fileName, Boolean reuseFirstSource);
    // called only by createNew();
    virtual ~AmbaVideoServerMediaSubsession();

    // redefined virtual functions
    virtual RTCPInstance* createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
                                     unsigned char const* cname, RTPSink* sink);

    virtual FramedSource *createNewStreamSource(unsigned clientSessionId, unsigned &estBitrate);

    virtual RTPSink *createNewRTPSink(Groupsock *rtpGroupsock,
                                      unsigned char rtpPayloadTypeIfDynamic,
                                      FramedSource *inputSource);

    virtual void seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration, u_int64_t& numBytes);

private:
    // Private Parameters
    stream_type_t   fStream_type;
    u_int32_t       fStreamTag;
    stream_reader_t *fStream_reader;

    u_int8_t        *fSPS_payload;
    u_int32_t       fSPS_length;
    u_int8_t        *fPPS_payload;
    u_int32_t       fPPS_length;

    AMP_FORMAT_MID_e   fSourceCodec_ID;

    float       fDuration;

    RTPSink     *fVidRTPSink; // ditto
};

#endif
