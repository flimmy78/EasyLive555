#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rtsp_util_def.h"
#include "AmbaVideoStreamSource.h"
//=============================================================================
//                Constant Definition
//=============================================================================
#define MAX_VIDEO_FIFO_CACHE_SIZE_LIVE            (512 << 10) // 512KB
#define MAX_VIDEO_FIFO_CACHE_SIZE_PLAYBACK        (6 << 20)   // 5MB
//=============================================================================
//                Macro Definition
//=============================================================================
typedef enum VID_OPERATING_STATE
{
    VID_OPERATING_STATE_PLAY,
    VID_OPERATING_STATE_PAUSE,
    VID_OPERATING_STATE_SEEK,
} VID_OPERATING_STATE_T;
//=============================================================================
//                Structure Definition
//=============================================================================

//=============================================================================
//                Global Data Definition
//=============================================================================
////////// AmbaVideoStreamSource //////////
AmbaVideoStreamSource *
AmbaVideoStreamSource::createNew(UsageEnvironment &env, stream_reader_t *pStream_reader,
                                 char const *fileName, u_int32_t StreamTag)
{
    AmbaVideoStreamSource   *newSource = NULL;

    do{
        newSource = new AmbaVideoStreamSource(env, pStream_reader, fileName, StreamTag);
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

AmbaVideoStreamSource
::AmbaVideoStreamSource(UsageEnvironment &env, stream_reader_t *pStream_reader,
                        char const *fileName, u_int32_t StreamTag)
    : FramedSource(env), fStream_reader(pStream_reader), fFifo_reader(NULL),
      fStartQueueFrames(False), fuSec_90k_converter(0.0)
{
    int                         rval = 0;
    stream_fifo_reader_init_t   init_info;

    fFrameSize = 0;

    fCurFramePTS   = 0;
    fFirstFramePTS = 0;
    fPrevFrameTime = 0;
    fRemainLen     = 0;
    fStreamTag     = StreamTag;
    fCurFramePTSInMicrosec  = 0;
    fVideoStartTime.tv_sec  = 0;
    fVideoStartTime.tv_usec = 0;
    fPresentationTime.tv_sec = 0;
    fPresentationTime.tv_sec = 0;

    fOperatingState = VID_OPERATING_STATE_PLAY;
    fSeekTime       = 0;
    fCodec_id       = AMP_FORMAT_MID_UNKNOW;

    memset(&init_info, 0x0, sizeof(stream_fifo_reader_init_t));

    init_info.fifo_type = STREAM_TRACK_VIDEO;

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
        ctrl_box.pExtra_data      = (void*)STREAM_TRACK_VIDEO;
        ctrl_box.in.pb_open.pPath = (char*)fileName;
        rval = StreamReader_Control(fStream_reader, NULL, &ctrl_box);
        if( rval  )
        {
            return;
        }

        init_info.target_stream_id = ctrl_box.out.pb_open.stream_id;
        init_info.stream_type     = STREAM_TYPE_PLAYBACK;
        init_info.fifo_cache_size = MAX_VIDEO_FIFO_CACHE_SIZE_PLAYBACK;
    }
    else
    {
        init_info.stream_type     = STREAM_TYPE_LIVE;
        init_info.fifo_cache_size = MAX_VIDEO_FIFO_CACHE_SIZE_LIVE;

        fStream_type = STREAM_TYPE_LIVE;
    }

    rval = StreamReader_OpenVFifoReader(fStream_reader, &init_info, &fFifo_reader);
    if( rval )
    {
        err_msg("create video virtual fifo fail !!\n");
        return;
    }

    fCodec_id = fFifo_reader->codec_id;

    if( fStream_type == STREAM_TYPE_PLAYBACK )
    {
        stream_reader_ctrl_box_t    ctrl_box;
        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));
        ctrl_box.cmd         = STREAM_READER_CMD_PLAYBACK_GET_VID_FTIME;
        ctrl_box.stream_tag  = fStreamTag;
        ctrl_box.stream_type = fStream_type;
        StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);

        fuSecsPerFrame = ctrl_box.out.get_vid_frm_time.frame_time * 1000;
    }

    StreamReader_StartVFifo(fStream_reader, fFifo_reader);
    fStartQueueFrames = True;
    return;
}

AmbaVideoStreamSource::~AmbaVideoStreamSource()
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

void AmbaVideoStreamSource::doGetNextFrame()
{
    u_int32_t   valid_start_code = (u_int32_t)(-1);
    u_int8_t    *pPrev_valid_nal_addr = NULL;

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

                fOperatingState = VID_OPERATING_STATE_PLAY;
            }
        }
        else
        {
            // delay ms
            nextTask() = envir().taskScheduler().scheduleDelayedTask(25000, (TaskFunc*)retryLater, this);
            return;
        }
    }

    if( fStream_type == STREAM_TYPE_PLAYBACK &&
        fOperatingState != VID_OPERATING_STATE_PLAY )
    {
        stream_reader_ctrl_box_t    ctrl_box;
        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));

        ctrl_box.stream_tag = fStreamTag;
        if( fOperatingState == VID_OPERATING_STATE_SEEK )
        {
            ctrl_box.cmd                  = STREAM_READER_CMD_PLAYBACK_PLAY;
            ctrl_box.stream_type          = fStream_type;
            ctrl_box.in.pb_play.seek_time = fSeekTime;
            StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);
        }

        fOperatingState = VID_OPERATING_STATE_PLAY;
    }

    if( fVideoStartTime.tv_sec == 0 && fVideoStartTime.tv_usec == 0 )
    {
        // This is the first frame, so use the current time:
        gettimeofday(&fVideoStartTime, NULL);
    }

    // Get next frame or not
    if( fRemainLen == 0 )
    {
        stream_reader_frame_info_t  frame_info = {0};

        do{
            StreamReader_GetFrame(fStream_reader, fFifo_reader, &frame_info);
            if( frame_info.frame_size == 0 || frame_info.pFrame_addr == NULL )
            {
                fRemainLen    = 0;
                fCurParseAddr = NULL;
                // delay ms
                if( fStream_type == STREAM_TYPE_PLAYBACK )
                    nextTask() = envir().taskScheduler().scheduleDelayedTask(5000, (TaskFunc*)retryLater, this);
                else
                    nextTask() = envir().taskScheduler().scheduleDelayedTask(10000, (TaskFunc*)retryLater, this);

                return;
            }

            if( frame_info.frame_type == AMBA_FRAMEINFO_TYPE_EOS )
            {
                fRemainLen = fFrameSize = 0;
                StreamReader_FreeFrame(fStream_reader, fFifo_reader, NULL);
                handleClosure();
                return;
            }

            fRemainLen    = frame_info.frame_size;
            fCurParseAddr = frame_info.pFrame_addr;
            fCurFramePTS  = (int64_t)frame_info.pts;
            fFirstFramePTS = (fFirstFramePTS) ? fFirstFramePTS : fCurFramePTS;

            /* If wrong start code (this case is not expected), get frame again. */
        }while( !(fCurParseAddr[0] == 0x00 && fCurParseAddr[1] == 0x00 && fCurParseAddr[2] == 0x00 && fCurParseAddr[3] == 0x01) );

        //dbg_msg("pts=%lld, frameSzie=%u\n", fCurFramePTS, fRemainLen);

        if( fStream_type == STREAM_TYPE_PLAYBACK )
        {
            if( fuSec_90k_converter == 0.0 )
                fuSec_90k_converter = (float)1000000.0 / 90000.0;

            #if 0
            fCurFramePTSInMicrosec = (u_int64_t)((fCurFramePTS - fFirstFramePTS) * fuSec_90k_converter); //convert to micro sec with 90K base
            #else
            fCurFramePTSInMicrosec += fuSecsPerFrame;
            #endif
        }
        else
        {
            if( fuSec_90k_converter == 0.0 )
                fuSec_90k_converter = (float)1000000.0 / frame_info.u.video.time_scale;

            // calculate time stamp
            fCurFramePTSInMicrosec = (u_int64_t)((fCurFramePTS - fFirstFramePTS) * fuSec_90k_converter); //convert to micro sec with 90K base
        }

        // time stamp is backdated.
        if( fPrevFrameTime && fCurFramePTSInMicrosec == 0 )
        {
            fFirstFramePTS = 0;
            gettimeofday(&fVideoStartTime, NULL);
        }

        u_int64_t   uSecond = fVideoStartTime.tv_usec + fCurFramePTSInMicrosec;
        fPresentationTime.tv_sec  = uSecond / 1000000;
        fPresentationTime.tv_usec = uSecond % 1000000;
        fPresentationTime.tv_sec  += fVideoStartTime.tv_sec;
    }

    // out Nal unit every time
    pPrev_valid_nal_addr = NULL;
    while( fRemainLen > 0 )
    {
        valid_start_code = (valid_start_code << 8) | *fCurParseAddr;
        fCurParseAddr++;
        fRemainLen--;

        if( valid_start_code == 0x00000001 )
        {
            u_int8_t     nal_type = (*fCurParseAddr & 0x1F);

            // fprintf(stderr, "nal type=%d\n", nal_type);

            if( pPrev_valid_nal_addr )
            {
                // parse at start code when next time
                fCurParseAddr -= 4;
                fRemainLen += 4;

                fFrameSize = (fCurParseAddr - pPrev_valid_nal_addr);
                memmove(fTo, pPrev_valid_nal_addr, fFrameSize);

                fDurationInMicroseconds = 0;
                FramedSource::afterGetting(this);
                return;
            }

            if( nal_type < 6 )
            {
                fFrameSize = fRemainLen;
                memmove(fTo, fCurParseAddr, fFrameSize);

                fRemainLen = 0;
                StreamReader_FreeFrame(fStream_reader, fFifo_reader, NULL);

                // update duration
                if( fPrevFrameTime )
                {
                    int32_t     duration = (fCurFramePTSInMicrosec - fPrevFrameTime);

                    fDurationInMicroseconds = (duration > 0 && duration < 55000)
                                            ? (unsigned)duration : 10000;
                }
                else
                    fDurationInMicroseconds = 33000;

                fPrevFrameTime = fCurFramePTSInMicrosec;

                //printf("v_pts: %u.%06u\n", fPresentationTime.tv_sec, fPresentationTime.tv_usec);
                // dbg_msg("------ fDurationInUS=%u, (pts_us=%u), fFrameSize=%u\n",
                //         fDurationInMicroseconds,
                //         cur_frame_pts_us,
                //         fFrameSize);

                FramedSource::afterGetting(this);
                return;
            }
            else if( nal_type == 7 || nal_type == 8 )
            {
                pPrev_valid_nal_addr = fCurParseAddr;
            }
            else
                pPrev_valid_nal_addr = NULL;

        }
    }

    // no find target nal unit, re-start in next frame
    StreamReader_FreeFrame(fStream_reader, fFifo_reader, NULL);
    nextTask() = envir().taskScheduler().scheduleDelayedTask(2000, (TaskFunc*)retryLater, this);
    return;
}

int AmbaVideoStreamSource::getSpsPps(u_int8_t **ppSPS_payload, u_int32_t *pSPS_length,
                                     u_int8_t **ppPPS_payload, u_int32_t *pPPS_length)
{
    int                         result = 0;
    uint32_t                    timeout = 20;
    stream_specific_info_t      specific_info;

    if( ppSPS_payload == NULL || pSPS_length == NULL ||
        ppPPS_payload == NULL || pPPS_length == NULL )
        return -1;

    memset(&specific_info, 0x0, sizeof(stream_specific_info_t));

    do{
        specific_info.codec_id = fFifo_reader->codec_id;
        result = StreamReader_GetSpecificInfo(fStream_reader, fFifo_reader, &specific_info);
        if( result )
        {
            err_msg("Wrong fifo reader, can't get SPS/PPS !\n");
            result = -2;
            break;
        }

        *pSPS_length   = specific_info.u.h264.sps_length;
        *ppSPS_payload = specific_info.u.h264.pSPS_data;
        *pPPS_length   = specific_info.u.h264.pps_length;
        *ppPPS_payload = specific_info.u.h264.pPPS_data;
        if( *pSPS_length == 0 || *pPPS_length == 0 ||
            *ppSPS_payload == NULL || *ppPPS_payload == NULL )
        {
            err_msg("Can't get SPS(%p, %u)/PPS(%p, %u) !\n",
                    *ppSPS_payload, *pSPS_length,
                    *ppPPS_payload, *pPPS_length);
            result = -3;
        }
        usleep(100000);
        //sleep(1);
        timeout--;
    }while( timeout && (*pSPS_length == 0 || *pPPS_length == 0) );

#if 0
    if( *pSPS_length && *pPPS_length )
    {
        u_int32_t   i;

        fprintf(stderr, "\n\t=> live SPS (%u): \n\t", specific_info.u.h264.sps_length);
        for(i = 0; i < specific_info.u.h264.sps_length; i++)
        {
            fprintf(stderr, "%02x ", specific_info.u.h264.pSPS_data[i]);
        }

        fprintf(stderr, "\n\t=> live PPS (%u): \n\t", specific_info.u.h264.pps_length);
        for(i = 0; i < specific_info.u.h264.pps_length; i++)
        {
            fprintf(stderr, "%02x ", specific_info.u.h264.pPPS_data[i]);
        }
        fprintf(stderr, "\n\n");
    }
#endif

    StreamReader_StopVFifo(fStream_reader, fFifo_reader);
    fStartQueueFrames = False;
    return result;
}

void AmbaVideoStreamSource::retryLater(void *firstArg)
{
    AmbaVideoStreamSource *pVideoStreamSource = (AmbaVideoStreamSource *)firstArg;

    pVideoStreamSource->doGetNextFrame();
}

Boolean AmbaVideoStreamSource::isH264VideoStreamFramer() const
{
    if( fFifo_reader->codec_id == AMP_FORMAT_MID_H264 ||
        fFifo_reader->codec_id == AMP_FORMAT_MID_AVC )
        return True;
    else
        return False;
}

Boolean AmbaVideoStreamSource::isH265VideoStreamFramer() const
{
    return False;
}

unsigned AmbaVideoStreamSource::maxFrameSize() const
{
    // By default, this source has no maximum frame size.
    return 0;//fFrameSize;
}

void AmbaVideoStreamSource::doStopGettingFrames()
{
    if( fStream_type == STREAM_TYPE_PLAYBACK )
    {
        stream_reader_ctrl_box_t    ctrl_box;
        memset(&ctrl_box, 0x0, sizeof(stream_reader_ctrl_box_t));
        ctrl_box.cmd         = STREAM_READER_CMD_PLAYBACK_PAUSE;
        ctrl_box.stream_type = fStream_type;
        ctrl_box.stream_tag  = fStreamTag;
        StreamReader_Control(fStream_reader, fFifo_reader, &ctrl_box);
    }
    else
    {
        StreamReader_StopVFifo(fStream_reader, fFifo_reader);
        fStartQueueFrames = False;
    }

    fVideoStartTime.tv_sec  = 0;
    fVideoStartTime.tv_usec = 0;
    fFirstFramePTS          = 0;
    fCurFramePTSInMicrosec  = 0;

    fOperatingState = VID_OPERATING_STATE_PAUSE;

    envir().taskScheduler().unscheduleDelayedTask(nextTask());
}

void AmbaVideoStreamSource
::seekStream(int32_t seekNPT/* second */)
{
    StreamReader_StopVFifo(fStream_reader, fFifo_reader);
    fStartQueueFrames = False;

    fVideoStartTime.tv_sec  = 0;
    fVideoStartTime.tv_usec = 0;
    fFirstFramePTS          = 0;
    fCurFramePTSInMicrosec  = 0;
    fSeekTime               = seekNPT * 1000; // convert to ms

    fOperatingState = VID_OPERATING_STATE_SEEK;
}


