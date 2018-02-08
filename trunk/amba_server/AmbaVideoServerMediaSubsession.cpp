
#include "rtsp_util_def.h"
#include "AmbaVideoServerMediaSubsession.h"
#include "AmbaVideoStreamSource.h"
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
AmbaVideoServerMediaSubsession *
AmbaVideoServerMediaSubsession::createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                          char const *fileName, Boolean reuseFirstSource)
{
    return new AmbaVideoServerMediaSubsession(env, pStream_reader, fileName, reuseFirstSource);
}

AmbaVideoServerMediaSubsession
::AmbaVideoServerMediaSubsession(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                 char const *fileName, Boolean reuseFirstSource)
    : FileServerMediaSubsession(env, fileName, reuseFirstSource),
      fVidRTPSink(NULL)
{
    fSPS_payload   = NULL;
    fSPS_length    = 0;
    fPPS_payload   = NULL;
    fPPS_length    = 0;
    fStream_reader = pStream_reader;
    fStream_type   = STREAM_TYPE_LIVE;
    fStreamTag     = 0;
    fDuration      = 0.0f;

    if( fileName )
    {
        fStreamTag = calculateCRC((u_int8_t const*)fileName, (unsigned)strlen((const char*)fileName));
        fStream_type = STREAM_TYPE_PLAYBACK;
    }
    return;
}

AmbaVideoServerMediaSubsession::~AmbaVideoServerMediaSubsession()
{
}

FramedSource *AmbaVideoServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned &estBitrate)
{
    estBitrate = 2000; // kbps, estimate

    // Create the video source:
    AmbaVideoStreamSource   *pAmbaSource= NULL;

    pAmbaSource = (fStream_type == STREAM_TYPE_LIVE)
                ? AmbaVideoStreamSource::createNew(envir(), fStream_reader, NULL, (u_int32_t)fStreamTag)
                : AmbaVideoStreamSource::createNew(envir(), fStream_reader, fFileName, (u_int32_t)fStreamTag);

    fSourceCodec_ID = AMP_FORMAT_MID_UNKNOW;

    // get sps and pps from amba stream
    if( pAmbaSource )
    {
        int     result = 0;

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
            case AMP_FORMAT_MID_AVC:
            case AMP_FORMAT_MID_H264:
                result = pAmbaSource->getSpsPps(&fSPS_payload, &fSPS_length, &fPPS_payload, &fPPS_length);
                if( result )
                {
                    dbg_msg("No SPS/PPS !\n");
                    // Medium::close(pAmbaSource);
                    // pAmbaSource = NULL;
                }
                break;
            default:
                break;
        }
    }
    return pAmbaSource;
}

void AmbaVideoServerMediaSubsession
::rtcpRRQos(void* clientData)
{
    AmbaVideoServerMediaSubsession      *pSMSub = (AmbaVideoServerMediaSubsession*)clientData;

    if( pSMSub == NULL || pSMSub->fVidRTPSink == NULL )
        return;

    do {
        RTPTransmissionStatsDB::Iterator    statsIter(pSMSub->fVidRTPSink->transmissionStatsDB());
        RTPTransmissionStats                *pCur_stats = statsIter.next();

        if( pSMSub->fStream_type == STREAM_TYPE_LIVE )
        {
            stream_reader_ctrl_box_t    ctrl_box;
            stream_status_info_t        *pLive_stream_status = NULL;

            pLive_stream_status = &pSMSub->fStream_reader->live_stream_status;

            // use pLive_stream_status->extra_data to make audio session skip SEND_RR_STAT
            if( pLive_stream_status->extra_data &&
                pLive_stream_status->extra_data != (void*)STREAM_TRACK_VIDEO )
                break;

            memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

            ctrl_box.cmd                                = STREAM_READER_CMD_SEND_RR_STAT;
            ctrl_box.stream_tag                         = pSMSub->fStreamTag;
            ctrl_box.stream_type                        = pSMSub->fStream_type;
            ctrl_box.pExtra_data                        = (void*)STREAM_TRACK_VIDEO;
            ctrl_box.in.send_rr_stat.fraction_lost      = pCur_stats->packetLossRatio();
            ctrl_box.in.send_rr_stat.jitter             = pCur_stats->jitter();
            ctrl_box.in.send_rr_stat.propagation_delay  = pCur_stats->roundTripDelay() / 65536.0f; // 64KHz
            StreamReader_Control(pSMSub->fStream_reader, NULL, &ctrl_box);
        }

#if 0
        while( pCur_stats )
        {
            fprintf(stderr, "\tV:packetId=%u, lossRate=%u, allPacketsLost=%u, jitter=%u, lsr=%u, dlsr=%u, delay=%u\n",
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

RTCPInstance* AmbaVideoServerMediaSubsession
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

RTPSink *AmbaVideoServerMediaSubsession
::createNewRTPSink(Groupsock *rtpGroupsock,
                   unsigned char rtpPayloadTypeIfDynamic,
                   FramedSource * /*inputSource*/)
{
    switch( fSourceCodec_ID )
    {
        case AMP_FORMAT_MID_AVC:
        case AMP_FORMAT_MID_H264:
            fVidRTPSink = H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic,
                                                      fSPS_payload, fSPS_length, fPPS_payload, fPPS_length);
            break;
        default:
            break;
    }

    //dbg_msg("fVidRTPSink = %p\n", fVidRTPSink);
    return fVidRTPSink;
}

void AmbaVideoServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration,
                   u_int64_t& /*numBytes*/)
{
    AmbaVideoStreamSource   *pAmbaSource= (AmbaVideoStreamSource*)inputSource;

    if( (float)seekNPT < fDuration )
        pAmbaSource->seekStream((int32_t)seekNPT);
}

float AmbaVideoServerMediaSubsession::duration() const
{
    return fDuration;
}


