
#include "rtsp_util_def.h"
#include "AmbaAudioServerMediaSubsession.h"
#include "AmbaAudioStreamSource.h"
#include "MPEG2TransportStreamMultiplexor.hh"
//=============================================================================
//                Constant Definition
//=============================================================================

//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================

//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================

//=============================================================================
//                Public Function Definition
//=============================================================================
AmbaAudioServerMediaSubsession *
AmbaAudioServerMediaSubsession::createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
        char const *fileName, Boolean reuseFirstSource)
{
    return new AmbaAudioServerMediaSubsession(env, pStream_reader, fileName, reuseFirstSource);
}

AmbaAudioServerMediaSubsession
::AmbaAudioServerMediaSubsession(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                 char const *fileName, Boolean reuseFirstSource)
    : FileServerMediaSubsession(env, fileName, reuseFirstSource),
      fAudRTPSink(NULL)
{
    fStream_type   = STREAM_TYPE_LIVE;
    fStreamTag     = 0;
    fDuration      = 0.0f;
    fStream_reader = pStream_reader;

    if( fileName )
    {
        fStreamTag = calculateCRC((u_int8_t const*)fileName, (unsigned)strlen((const char*)fileName));
        fStream_type = STREAM_TYPE_PLAYBACK;
    }
}

AmbaAudioServerMediaSubsession
::~AmbaAudioServerMediaSubsession()
{
}

void AmbaAudioServerMediaSubsession
::rtcpRRQos(void* clientData)
{
    AmbaAudioServerMediaSubsession      *pSMSub = (AmbaAudioServerMediaSubsession*)clientData;

    if( pSMSub == NULL || pSMSub->fAudRTPSink == NULL )
        return;

    do {
        RTPTransmissionStatsDB::Iterator    statsIter(pSMSub->fAudRTPSink->transmissionStatsDB());
        RTPTransmissionStats                *pCur_stats = statsIter.next();

        if( pSMSub->fStream_type == STREAM_TYPE_LIVE )
        {
            stream_reader_ctrl_box_t    ctrl_box;
            stream_status_info_t        *pLive_stream_status = NULL;

            pLive_stream_status = &pSMSub->fStream_reader->live_stream_status;

            // use pLive_stream_status->extra_data to make audio session skip SEND_RR_STAT
            if( pLive_stream_status->extra_data &&
                pLive_stream_status->extra_data != (void*)STREAM_TRACK_AUDIO )
                break;

            memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

            ctrl_box.cmd                                = STREAM_READER_CMD_SEND_RR_STAT;
            ctrl_box.stream_tag                         = pSMSub->fStreamTag;
            ctrl_box.stream_type                        = pSMSub->fStream_type;
            ctrl_box.pExtra_data                        = (void*)STREAM_TRACK_AUDIO;
            ctrl_box.in.send_rr_stat.fraction_lost      = pCur_stats->packetLossRatio();
            ctrl_box.in.send_rr_stat.jitter             = pCur_stats->jitter();
            ctrl_box.in.send_rr_stat.propagation_delay  = pCur_stats->roundTripDelay() / 65536.0f; // 64KHz
            StreamReader_Control(pSMSub->fStream_reader, NULL, &ctrl_box);
        }
#if 0
        while( pCur_stats )
        {
            fprintf(stderr, "\tA:packetId=%u, lossRate=%u, allPacketsLost=%u, jitter=%u, lsr=%u, dlsr=%u, delay=%u\n",
                    pCur_stats->lastPacketNumReceived(),
                    pCur_stats->packetLossRatio(),
                    pCur_stats->totNumPacketsLost(),
                    pCur_stats->jitter(),
                    pCur_stats->lastSRTime(),
                    pCur_stats->diffSR_RRTime(),
                    pCur_stats->roundTripDelay()); // 64KHz

            pCur_stats = statsIter.next();
        }
#endif
    } while(0);

    return;
}

RTCPInstance* AmbaAudioServerMediaSubsession
::createRTCP(Groupsock* RTCPgs, unsigned totSessionBW, /* in kbps */
             unsigned char const* cname, RTPSink* sink)
{
    // Default implementation:
    RTCPInstance      *pRTCPInstance = NULL;

    pRTCPInstance = RTCPInstance::createNew(envir(), RTCPgs, totSessionBW,
                                            cname, sink, NULL/*we're a server*/);
    if( pRTCPInstance )
        pRTCPInstance->setRRHandler(rtcpRRQos, this);

    return pRTCPInstance;
}

FramedSource *AmbaAudioServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned &estBitrate)
{
    estBitrate = 128; // kbps, estimate

    // Create the audio source:
    AmbaAudioStreamSource   *pAmbaSource= NULL;

    pAmbaSource = (fStream_type == STREAM_TYPE_LIVE)
                ? AmbaAudioStreamSource::createNew(envir(), fStream_reader, NULL, fStreamTag)
                : AmbaAudioStreamSource::createNew(envir(), fStream_reader, fFileName, fStreamTag);

    fSourceCodec_ID = AMP_FORMAT_MID_UNKNOW;

    if( pAmbaSource )
    {
        fSourceCodec_ID = pAmbaSource->getSourceCodecID();

        dbg_msg("fSourceCodec_ID=0x%x\n", fSourceCodec_ID);

        if( fStream_type == STREAM_TYPE_PLAYBACK )
        {
            stream_reader_ctrl_box_t    ctrl_box;

            memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

            ctrl_box.cmd         = STREAM_READER_CMD_PLAYBACK_GET_DURATION;
            ctrl_box.stream_tag  = fStreamTag;
            ctrl_box.stream_type = fStream_type;
            StreamReader_Control(fStream_reader, NULL, &ctrl_box);

            fDuration = (float)ctrl_box.out.get_duration.duration / 1000.0;
        }

        switch( fSourceCodec_ID )
        {
            case AMP_FORMAT_MID_AAC:
                return pAmbaSource;
                break;

            case AMP_FORMAT_MID_PCM:
            case AMP_FORMAT_MID_ADPCM:
            case AMP_FORMAT_MID_MP3:
            case AMP_FORMAT_MID_AC3:
            case AMP_FORMAT_MID_WMA:
            case AMP_FORMAT_MID_OPUS:
                break;
            default:
                break;
        }
    }
    return NULL;
}

RTPSink *AmbaAudioServerMediaSubsession
::createNewRTPSink(Groupsock *rtpGroupsock,
                   unsigned char rtpPayloadTypeIfDynamic,
                   FramedSource *inputSource)
{
    AmbaAudioStreamSource   *pADTSSource = (AmbaAudioStreamSource *)inputSource;
    fAudRTPSink = MPEG4GenericRTPSink::createNew(envir(), rtpGroupsock,
                                    rtpPayloadTypeIfDynamic,
                                    pADTSSource->samplingFrequency(),
                                    "audio", "AAC-hbr", pADTSSource->configStr(),
                                    pADTSSource->numChannels());

    return fAudRTPSink;
}

void AmbaAudioServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration,
                   u_int64_t& /*numBytes*/)
{
    AmbaAudioStreamSource   *pAmbaSource= (AmbaAudioStreamSource*)inputSource;

    if( (float)seekNPT < fDuration )
        pAmbaSource->seekStream((int32_t)seekNPT);
}

float AmbaAudioServerMediaSubsession::duration() const
{
    return fDuration;
}

