
#include "rtsp_util_def.h"
#include "AmbaAudioStreamSource.h"
//=============================================================================
//                Constant Definition
//=============================================================================
#define MAX_AUDIO_FIFO_CACHE_SIZE_LIVE            (512 << 10) // 512KB
#define MAX_AUDIO_FIFO_CACHE_SIZE_PLAYBACK        (1 << 20)   // 1MB

#define AAC_MAIN_PROFILE        1
#define AAC_LC_PROFILE          2

typedef enum AUD_OPERATING_STATE
{
    AUD_OPERATING_STATE_PLAY,
    AUD_OPERATING_STATE_PAUSE,
    AUD_OPERATING_STATE_SEEK,
} AUD_OPERATING_STATE_T;
//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================

//=============================================================================
//                Global Data Definition
//=============================================================================
////////// AmbaAudioStreamSource //////////
static unsigned const samplingFrequencyTable[16] =
{
    96000, 88200, 64000, 48000,
    44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000,
    7350, 0, 0, 0
};


AmbaAudioStreamSource *
AmbaAudioStreamSource::createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                 char const *fileName, u_int32_t StreamTag)
{
    AmbaAudioStreamSource   *newSource = NULL;
    do{
        newSource =  new AmbaAudioStreamSource(env, pStream_reader, fileName, StreamTag);
        #if 0
        if( newSource->fFifo_reader == NULL )
        {
            Medium::close(newSource);
            return NULL;
        }
        #endif
    }while(0);

    return newSource;
}

AmbaAudioStreamSource
::AmbaAudioStreamSource(UsageEnvironment &env, stream_reader_t *pStream_reader,
                        char const *fileName, u_int32_t StreamTag)
    : FramedSource(env), fStream_reader(pStream_reader), fFifo_reader(NULL)
{
    int                         rval = 0;
    stream_fifo_reader_init_t   init_info;

    fFrameSize = 0;

    fRemainLen          = 0;
    fSamplingFrequency  = 0;
    fNumChannels        = 0;
    fuSecsPerFrame      = 0.0f;
    fuSec_90k_converter = 1000000.0f / 90000.0f;
    fStreamTag          = StreamTag;
    fOperatingState     = AUD_OPERATING_STATE_PLAY;
    fSeekTime           = 0;
    fFirstFramePTS      = 0;

    fPrevFrameTime         = 0;
    fCurFramePTSInMicrosec = 0;

    fAudioStartTime.tv_sec    = 0;
    fAudioStartTime.tv_usec   = 0;
    fPresentationTime.tv_sec  = 0;
    fPresentationTime.tv_usec = 0;

    memset(&init_info, 0x0, sizeof(stream_fifo_reader_init_t));

    init_info.fifo_type = STREAM_TRACK_AUDIO;

    if( fileName )
    {
        // open
        int                         rval = 0;
        stream_reader_ctrl_box_t    ctrl_box;

        fStream_type = STREAM_TYPE_PLAYBACK;

        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

        ctrl_box.cmd              = STREAM_READER_CMD_PLAYBACK_OPEN;
        ctrl_box.stream_tag       = fStreamTag;
        ctrl_box.stream_type      = fStream_type;
        ctrl_box.in.pb_open.pPath = (char*)fileName;
        rval = StreamReader_Control(fStream_reader, NULL, &ctrl_box);
        if( rval  )
        {
            return;
        }

        init_info.target_stream_id = ctrl_box.out.pb_open.stream_id;
        init_info.stream_type      = STREAM_TYPE_PLAYBACK;
        init_info.fifo_cache_size  = MAX_AUDIO_FIFO_CACHE_SIZE_PLAYBACK;
    }
    else
    {
        init_info.stream_type     = STREAM_TYPE_LIVE;
        init_info.fifo_cache_size = MAX_AUDIO_FIFO_CACHE_SIZE_LIVE;

        fStream_type = STREAM_TYPE_LIVE;
    }

    rval = StreamReader_OpenVFifoReader(fStream_reader, &init_info, &fFifo_reader);
    if( rval )
    {
        err_msg("create audio virtual fifo fail !!\n");
        return;
    }

    fStartQueueFrames = False;

    fCodec_id           = fFifo_reader->codec_id;
    fSamplingFrequency  = fFifo_reader->u.audio.sample_rate;
    fNumChannels        = fFifo_reader->u.audio.channels;
    fuSecsPerFrame      = (1024.0f/*samples-per-frame*/*1000000.0f) / fSamplingFrequency/*samples-per-second*/;

    switch( fCodec_id )
    {
        case AMP_FORMAT_MID_AAC:
            {
                int             samplingFreqIdx = 0;
                unsigned char   audioSpecificConfig[2];
                uint8_t const   audioObjectType = AAC_LC_PROFILE;

                for(samplingFreqIdx = 0; samplingFreqIdx < 13; samplingFreqIdx++)
                {
                    if( samplingFrequencyTable[samplingFreqIdx] == fSamplingFrequency )
                        break;
                }

                if(  samplingFreqIdx > 12 )
                {
                    StreamReader_CloseVFifoReader(fStream_reader, &fFifo_reader);
                    err_msg("Not support Sampling Frequency %u !", fSamplingFrequency);
                    break;
                }

                audioSpecificConfig[0] = (audioObjectType<<3) | (samplingFreqIdx>>1);
                audioSpecificConfig[1] = (samplingFreqIdx<<7) | (fNumChannels<<3);
                snprintf(fConfigStr, 5, "%02X%02x", audioSpecificConfig[0], audioSpecificConfig[1]);
            }
            break;

        case AMP_FORMAT_MID_PCM:
        case AMP_FORMAT_MID_ADPCM:
        case AMP_FORMAT_MID_MP3:
        case AMP_FORMAT_MID_AC3:
        case AMP_FORMAT_MID_WMA:
        case AMP_FORMAT_MID_OPUS:
        default:
            err_msg("Not support audio codec 0x%x !\n", fCodec_id);
            break;
    }

    return;
}

AmbaAudioStreamSource::~AmbaAudioStreamSource()
{
    if( fStream_type == STREAM_TYPE_PLAYBACK )
    {
        stream_reader_ctrl_box_t    ctrl_box;

        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

        ctrl_box.cmd         = STREAM_READER_CMD_PLAYBACK_STOP;
        ctrl_box.stream_tag  = fStreamTag;
        ctrl_box.stream_type = fStream_type;
        StreamReader_Control(fStream_reader, NULL, &ctrl_box);
    }
    StreamReader_StopVFifo(fStream_reader, fFifo_reader);
    StreamReader_CloseVFifoReader(fStream_reader, &fFifo_reader);
}

void AmbaAudioStreamSource::doGetNextFrame()
{
    if( !fFifo_reader )
    {
        handleClosure();
        return;
    }

    if( fStartQueueFrames == False )
    {
        fStartQueueFrames = True;
        StreamReader_StartVFifo(fStream_reader, fFifo_reader);
        if( fStream_type == STREAM_TYPE_PLAYBACK )
        {
            stream_status_info_t        *pPB_status = NULL;
            stream_reader_ctrl_box_t    ctrl_box;
            int                         i;

            for(i = 0; i < STREAM_PLAYBACK_STATUS_NUM; i++)
            {
                if( fStream_reader->playback_status[i].stream_tag == fStreamTag )
                {
                    pPB_status = &fStream_reader->playback_status[i];
                    break;
                }
            }

            if( pPB_status &&
                pPB_status->state != STREAM_STATE_PLAYING )
            {
                memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));
                ctrl_box.cmd                  = STREAM_READER_CMD_PLAYBACK_PLAY;
                ctrl_box.stream_tag           = fStreamTag;
                ctrl_box.stream_type          = fStream_type;
                ctrl_box.in.pb_play.seek_time = fSeekTime;
                StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);
            }

            fOperatingState = AUD_OPERATING_STATE_PLAY;
        }
        else
        {
            // delay ms
            nextTask() = envir().taskScheduler().scheduleDelayedTask(25000, (TaskFunc*)retryLater, this);
            return;
        }
    }

    if( fStream_type == STREAM_TYPE_PLAYBACK &&
        fOperatingState != AUD_OPERATING_STATE_PLAY )
    {
        stream_reader_ctrl_box_t    ctrl_box;

        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));
        ctrl_box.stream_tag = fStreamTag;
        if( fOperatingState == AUD_OPERATING_STATE_SEEK )
        {
            ctrl_box.cmd                  = STREAM_READER_CMD_PLAYBACK_PLAY;
            ctrl_box.stream_type          = fStream_type;
            ctrl_box.in.pb_play.seek_time = fSeekTime;
            StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);
        }

        fOperatingState = AUD_OPERATING_STATE_PLAY;
    }

    if( fAudioStartTime.tv_sec == 0 && fAudioStartTime.tv_usec == 0 )
    {
        // This is the first frame, so use the current time:
        gettimeofday(&fAudioStartTime, NULL);
    }

    fDurationInMicroseconds = 0;

    if( fRemainLen == 0 )
    {
        stream_reader_frame_info_t  frame_info = {0};

        StreamReader_GetFrame(fStream_reader, fFifo_reader, &frame_info);
        if( frame_info.frame_size == 0 || frame_info.pFrame_addr == NULL )
        {
            fCurAddr = NULL;
            // delay ms
            nextTask() = envir().taskScheduler().scheduleDelayedTask(30000, (TaskFunc*)retryLater, this);
            return;
        }

        if( frame_info.frame_type == AMBA_FRAMEINFO_TYPE_EOS )
        {
            fRemainLen = fDurationInMicroseconds = 0;
            StreamReader_FreeFrame(fStream_reader, fFifo_reader, NULL);
            handleClosure();
            return;
        }
        else
        {
            u_int64_t   uSecond = 0;

            fRemainLen     = (int32_t)frame_info.frame_size;
            fCurAddr       = frame_info.pFrame_addr;
            fCurFramePTS   = (int64_t)frame_info.pts;
            fFirstFramePTS = (fFirstFramePTS) ? fFirstFramePTS : fCurFramePTS;

            if( fStream_type == STREAM_TYPE_PLAYBACK )
            {
                // Increment by the play time of the previous frame:
                fCurFramePTSInMicrosec += (unsigned)fuSecsPerFrame;
            }
            else
            {
                fCurFramePTSInMicrosec = (u_int64_t)((fCurFramePTS - fFirstFramePTS) * fuSec_90k_converter);
            }

            // time stamp is backdated.
            if( fPrevFrameTime && fCurFramePTSInMicrosec == 0 )
            {
                fFirstFramePTS = 0;
                gettimeofday(&fAudioStartTime, NULL);
            }

            uSecond = fAudioStartTime.tv_usec + fCurFramePTSInMicrosec;
            fPresentationTime.tv_sec  = uSecond / 1000000;
            fPresentationTime.tv_usec = uSecond % 1000000;
            fPresentationTime.tv_sec  += fAudioStartTime.tv_sec;

            //fprintf(stderr, "a: %d.%06d sec\n",
            //    fPresentationTime.tv_sec, fPresentationTime.tv_usec);

            fDurationInMicroseconds = (unsigned)fuSecsPerFrame;

            fPrevFrameTime = fCurFramePTSInMicrosec;
        }
    }

    fFrameSize = (fRemainLen > (int32_t)fMaxSize) ? fMaxSize : fRemainLen;

    if( fFrameSize )
        memmove(fTo, fCurAddr, fFrameSize);

    StreamReader_FreeFrame(fStream_reader, fFifo_reader, NULL);

    fCurAddr   += fFrameSize;
    fRemainLen -= fFrameSize;

    // Switch to another task, and inform the reader that he has data:
    nextTask() = envir().taskScheduler().scheduleDelayedTask(1000, (TaskFunc *)FramedSource::afterGetting, this);
    return;
}

void AmbaAudioStreamSource::retryLater(void *firstArg)
{
    AmbaAudioStreamSource *pAudioStreamSource = (AmbaAudioStreamSource *)firstArg;

    pAudioStreamSource->doGetNextFrame();
}

void AmbaAudioStreamSource::doStopGettingFrames()
{
    if( fStream_type == STREAM_TYPE_PLAYBACK )
    {
        stream_reader_ctrl_box_t    ctrl_box;
        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));
        ctrl_box.cmd         = STREAM_READER_CMD_PLAYBACK_PAUSE;
        ctrl_box.stream_tag  = fStreamTag;
        ctrl_box.stream_type = fStream_type;
        StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);
    }
    else
    {
        StreamReader_StopVFifo(fStream_reader, fFifo_reader);
        fStartQueueFrames = False;
    }

    fOperatingState = AUD_OPERATING_STATE_PAUSE;
    fAudioStartTime.tv_sec  = 0;
    fAudioStartTime.tv_usec = 0;
    fFirstFramePTS          = 0;
    fCurFramePTSInMicrosec  = 0;
    envir().taskScheduler().unscheduleDelayedTask(nextTask());
}

void AmbaAudioStreamSource
::seekStream(int32_t seekNPT /* second */)
{
    StreamReader_StopVFifo(fStream_reader, fFifo_reader);
    fStartQueueFrames = False;

    fAudioStartTime.tv_sec  = 0;
    fAudioStartTime.tv_usec = 0;
    fFirstFramePTS          = 0;
    fCurFramePTSInMicrosec  = 0;

    fSeekTime = seekNPT * 1000;

    fOperatingState = AUD_OPERATING_STATE_SEEK;
}

