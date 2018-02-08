
#ifndef _AMBA_AUDIO_SERVER_MEDIA_SUBSESSION_H
#define _AMBA_AUDIO_SERVER_MEDIA_SUBSESSION_H

#ifndef _ADTS_AUDIO_FILE_SERVER_MEDIA_SUBSESSION_HH
    #include "ADTSAudioFileServerMediaSubsession.hh"
#endif

#ifndef _MPEG4_GENERIC_RTP_SINK_HH
    #include "MPEG4GenericRTPSink.hh"
#endif

#ifndef _RTCP_HH
    #include "RTCP.hh"
#endif

#include "stream_reader.h"
//=============================================================================
//                  Class Definition
//=============================================================================
class AmbaAudioServerMediaSubsession: public FileServerMediaSubsession
{
public:
    static AmbaAudioServerMediaSubsession *
    createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
              char const *fileName, Boolean reuseFirstSource);

    virtual float duration() const;

    static void rtcpRRQos(void* clientData);

protected:
    AmbaAudioServerMediaSubsession(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                   char const *fileName, Boolean reuseFirstSource);
    // called only by createNew();
    virtual ~AmbaAudioServerMediaSubsession();

    virtual RTCPInstance* createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
                                     unsigned char const* cname, RTPSink* sink);

    virtual FramedSource *createNewStreamSource(unsigned clientSessionId, unsigned &estBitrate);

    virtual RTPSink *createNewRTPSink(Groupsock *rtpGroupsock,
                                      unsigned char rtpPayloadTypeIfDynamic,
                                      FramedSource *inputSource);

    virtual void seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration, u_int64_t& numBytes);

private:
    // Private Parameters
    stream_type_t       fStream_type;
    u_int32_t           fStreamTag;
    stream_reader_t     *fStream_reader;
    AMP_FORMAT_MID_e    fSourceCodec_ID;

    float       fDuration;

    RTPSink     *fAudRTPSink;  // ditto
};

#endif
