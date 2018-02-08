


#include "util_def.h"
#include "stream_fifo.h"
#include "stream_reader.h"

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
static int
playback_fifo_create(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;
    AMBA_NETFIFO_MEDIA_TRACK_CFG_s  *pTrack_info = 0;
    AMBA_NETFIFO_CFG_s              cfg = {0};

    pTrack_info = (AMBA_NETFIFO_MEDIA_TRACK_CFG_s*)pfifo_param->u.create_arg.pTrack_info;

    do{
        result = AmbaNetFifo_GetDefaultCFG(&cfg);
        if( result < 0 )
        {
            err_msg("Fail to do AmbaNetFifo_GetDefaultCFG()\n");
            break;
        }

        cfg.hCodec     = pTrack_info->hCodec;
        cfg.cbEvent    = pfifo_param->u.create_arg.cbEvent;
        cfg.NumEntries = 256;
        cfg.IsVirtual  = 1;

        pthread_mutex_lock(pMutex);

        pVfifo_info->pFifo_handle = (AMBA_NETFIFO_HDLR_s*)AmbaNetFifo_Create(&cfg);
        if( pVfifo_info->pFifo_handle == NULL )
        {
            err_msg("Fail to do AmbaNetFifo_Create() !\n");
            result = -1;
            pthread_mutex_unlock(pMutex);
            break;
        }
        else
        {
            dbg_msg("\n\t@@@@ Create new fifo %p for hCodec %p (CodecId=0x%x) success!\n"
                    "\t@@@@ TimeScale    =%u\n"
                    "\t@@@@ TimePerFrame =%u\n"
                    "\t@@@@ InitDelay    =%u\n",
                    pVfifo_info->pFifo_handle, cfg.hCodec, pTrack_info->nMediaId,
                    pTrack_info->nTimeScale,
                    pTrack_info->nTimePerFrame,
                    pTrack_info->nInitDelay);

            pVfifo_info->pBufBase  = (void *)(pTrack_info->pBufferBase);
            pVfifo_info->pBufLimit = (void *)(pTrack_info->pBufferLimit);
        }

        pVfifo_info->codec_id = pTrack_info->nMediaId;

        switch( track_type )
        {
            case AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO:
                pVfifo_info->u.audio.sample_rate     = (unsigned int)pTrack_info->Info.Audio.nSampleRate;
                pVfifo_info->u.audio.channels        = (unsigned int)pTrack_info->Info.Audio.nChannels;
                pVfifo_info->u.audio.bits_per_sample = (unsigned int)pTrack_info->Info.Audio.nBitsPerSample;
                dbg_msg("\n\t@@@@ BitsPerSample=%u\n"
                        "\t@@@@ Channels     =%u\n"
                        "\t@@@@ SampleRate   =%u\n",
                        pVfifo_info->u.audio.bits_per_sample,
                        pVfifo_info->u.audio.channels,
                        pVfifo_info->u.audio.sample_rate);
                break;

            case AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO:
                pVfifo_info->u.video.time_scale       = (unsigned int)pTrack_info->nTimeScale;
                pVfifo_info->u.video.time_per_frame   = (unsigned int)pTrack_info->nTimePerFrame;
                pVfifo_info->u.video.init_delay       = (unsigned int)pTrack_info->nInitDelay;
                pVfifo_info->u.video.width            = (unsigned int)pTrack_info->Info.Video.nWidth;
                pVfifo_info->u.video.height           = (unsigned int)pTrack_info->Info.Video.nHeight;
                dbg_msg("\n\t@@@@ width = %u\n"
                        "\t@@@@ height= %u\n",
                        pVfifo_info->u.video.width,
                        pVfifo_info->u.video.height);
                break;
            default:
                break;
        }

        pthread_mutex_unlock(pMutex);
    }while(0);

    return result;
}

static int
playback_fifo_destroy(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;

    switch( track_type )
    {
        case AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO:
        case AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO:
            pthread_mutex_lock(pMutex);
            if( pVfifo_info->pFifo_handle )
            {
                result = AmbaNetFifo_Delete(pVfifo_info->pFifo_handle);
                dbg_msg("Delete fifo %p, rval = %d\n", pVfifo_info->pFifo_handle, result);
                pVfifo_info->pFifo_handle = NULL;
            }
            pthread_mutex_unlock(pMutex);
            break;
        default:
            break;
    }
    return result;
}
//=============================================================================
//                Public Function Definition
//=============================================================================
stream_fifo_desc_t   amba_stream_playback_desc =
{
    NULL,                   // struct stream_fifo_fifo_desc  *next;
    "playback",             // const char          *name;
    STREAM_TYPE_PLAYBACK,   // unsigned int        stream_type;
    playback_fifo_create,   // int     (*vfifo_create)(stream_fifo_fifo_param_t *pfifo_info);
    playback_fifo_destroy,  // int     (*vfifo_destroy)(stream_fifo_fifo_param_t *pfifo_info);
};

