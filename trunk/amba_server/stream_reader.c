
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pthread.h"
#include "AmbaFrameInfo.h"
#include "AmbaNetFifo.h"

#include "util_def.h"
#include "stream_fifo.h"
#include "stream_reader.h"

//=============================================================================
//                Constant Definition
//=============================================================================
#define MAX_VFIFO_NUM       4
//=============================================================================
//                Macro Definition
//=============================================================================
/**
 * handle check function
 **/
#define _verify_handle(handle, err_code)        \
            do{ if(handle==NULL){               \
                err_msg("Null pointer !!\n");   \
                return err_code;}               \
            }while(0)

#ifndef MEMBER_OFFSET
#define MEMBER_OFFSET(type, member)     (uint32_t)&(((type *)0)->member)
#endif

#ifndef STRUCTURE_POINTER
#define STRUCTURE_POINTER(type, ptr, member)    (type*)((uint32_t)ptr - MEMBER_OFFSET(type, member))
#endif

#define REGISTER_STREAM_TYPE(name)                                  \
    do{ extern stream_fifo_desc_t   amba_stream_##name##_desc;      \
        stream_fifo_desc_t   **p;                                   \
        p = &pFirst_stream_fifo_desc;                               \
        while (*p != 0) p = &(*p)->next;                            \
        *p = &amba_stream_##name##_desc;                            \
        amba_stream_##name##_desc.next = 0;                         \
    }while(0)


#define REGISTER_CODEC(name)                                         \
    do{ extern stream_codec_desc_t   stream_codec_##name##_desc;     \
        stream_codec_desc_t   **p;                                   \
        p = &pFirst_stream_codec_desc;                               \
        while (*p != 0) p = &(*p)->next;                             \
        *p = &stream_codec_##name##_desc;                            \
        stream_codec_##name##_desc.next = 0;                         \
    }while(0)

#define FRM_BUF_SIZE_TO_PAUSE(total_size)       (((total_size) * 5) >> 4)
#define FRM_BUF_SIZE_TO_RESUME(total_size)      (((total_size) >> 1))

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  frame item in ring_frame_buffer
 *
 *  |           |
 *  +-----------+----------^--
 *  |frame item |          |
 *  | header    |      frame item
 *  +-----------+--^--     |
 *  | frame     |  |       |
 *  | header    |netfifo   |
 *  +-----------+  |       |
 *  | entropy   |  |       |
 *  +-----------+--v-------v--
 *  |           |
 *
 */
typedef struct frame_item_hdr
{
    unsigned long long      pts;
    uint32_t                frame_type; // ref to emum AMBA_FRAMEINFO_TYPE_e
    uint32_t                item_size;
    uint32_t                frame_num;

    uint32_t        frame_start_offset;
    uint32_t        frame_size;

} frame_item_hdr_t;

/**
 * stream fifo device
 */
typedef struct stream_fifo_dev
{
    stream_fifo_reader_t    fifo_reader;
    uint32_t                order_index;

    stream_fifo_desc_t      *pFifo_desc;

    // virtual fifo info, e.g. audio/video fifo
    vfifo_info_t            vfifo_info;

} stream_fifo_dev_t;

/**
 * stream device
 */
typedef struct stream_dev
{
    stream_reader_t     stream_reader;

    uint32_t            bAmbaNetfifo_Launched;

    // ambaNetFifo
    int                 event_handler;

    // virtual fifo, e.g. live/file stream fifo
    stream_fifo_dev_t   *ppFifo_dev[MAX_VFIFO_NUM];
    uint32_t            total_fifo_dev_cnt;

    pthread_mutex_t     mutex_stream_reader;

} stream_dev_t;

//=============================================================================
//                Global Data Definition
//=============================================================================
static stream_fifo_desc_t    *pFirst_stream_fifo_desc = 0;
static stream_codec_desc_t   *pFirst_stream_codec_desc = 0;
static pthread_t             g_tid_netfifo;

//=============================================================================
//                Private Function Definition
//=============================================================================
static void
_register_stream_desc()
{
    static int bRegistered = false;

    if( bRegistered == true )
        return;

    REGISTER_STREAM_TYPE(live);
    REGISTER_STREAM_TYPE(playback);

    bRegistered = true;
    return;
}

static stream_fifo_desc_t*
_find_stream_desc(stream_type_t type)
{
    stream_fifo_desc_t *dev = pFirst_stream_fifo_desc;
    while( dev )
    {
        if( dev->stream_type == type )
            return dev;

        dev = dev->next;
    }
    return 0;
}

static void
_register_stream_codec_desc()
{
    static int bCodec_registered = false;

    if( bCodec_registered == true )
        return;

    REGISTER_CODEC(h264);
    REGISTER_CODEC(avc);
    REGISTER_CODEC(aac);
    REGISTER_CODEC(pcm);

    bCodec_registered = true;
    return;
}

static stream_codec_desc_t*
_find_stream_codec_desc(AMP_FORMAT_MID_e codec_id)
{
    stream_codec_desc_t *dev = pFirst_stream_codec_desc;
    while( dev )
    {
        if( dev->codec_id == codec_id )
            return dev;
        dev = dev->next;
    }
    return 0;
}

/**
 * Callback function for NetFiFo event.
 */
static int
_cbFrameReady(void *handler, unsigned int event, void* info, void *user_data)
{
    int                 rval = 0;
    stream_dev_t        *pDev = (stream_dev_t*)user_data;

    if( pDev == NULL )
    {
        err_msg("Get NULL user_data !!\n");
        return 0;
    }

    switch( event )
    {
        case AMBA_NETFIFO_EVENT_DATA_READY:
            {
                int                             i;
                AMBA_NETFIFO_PEEKENTRY_ARG_s    entry = {0};
                AMBA_NETFIFO_REMOVEENTRY_ARG_s  del_entry = {0};
                AMBA_NETFIFO_BITS_DESC_s        desc = {0};
                AMBA_NETFIFO_HDLR_s             *pAct_netfifo_handle = NULL;
                stream_fifo_dev_t               *pAct_vfifo_dev = 0;

                // peek an entry
                entry.fifo                = (AMBA_NETFIFO_HDLR_s *)handler;
                entry.distanceToLastEntry = 0;
                rval = AmbaNetFifo_PeekEntry(&entry, &desc);
                if( rval )
                {
                    err_msg("Fail (%d) to do AmpFifo_PeekEntry when get NETFIFO_EVENT_DATA_READY !\n", rval);
                    return 0;
                }

                // check fifo handler type
                pthread_mutex_lock(&pDev->mutex_stream_reader);
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( pDev->ppFifo_dev[i] &&
                        handler == pDev->ppFifo_dev[i]->vfifo_info.pFifo_handle )
                    {
                        pAct_vfifo_dev = pDev->ppFifo_dev[i];

                        if( pAct_vfifo_dev->pFifo_desc &&
                            pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame )
                        {
                            int                     rval = 0;
                            stream_fifo_param_t     fifo_param = {0};

                            fifo_param.u.queue_frm_arg.pDesc            = &desc;
                            fifo_param.u.queue_frm_arg.pNetfifo_handle  = handler;
                            rval = pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame(&pAct_vfifo_dev->vfifo_info, &fifo_param);
                            if( rval )
                            {
                            }
                        }

                        // It is EOS
                        if( desc.Type == AMBA_FRAMEINFO_TYPE_DECODE_MARK ||
                            desc.Size == AMBA_NETFIFO_MARK_EOS )
                        {
                            dbg_msg("track %u {V=%u,A=%u} get EOS mark (type=%u, size=0x%x) !\n",
                                    pAct_vfifo_dev->vfifo_info.track_type,
                                    AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO,
                                    AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO,
                                    desc.Type, desc.Size);
                        }

                        pAct_netfifo_handle = handler;
                        break;
                    }
                }
                pthread_mutex_unlock(&pDev->mutex_stream_reader);

                if( pAct_netfifo_handle == NULL )
                {
                    dbg_msg("Unknown handler %p, frame type 0x%x. Skip\n", handler, desc.Type);
                    pAct_netfifo_handle = handler;
                }

                // remove current entry
                del_entry.fifo               = pAct_netfifo_handle;
                del_entry.EntriesToBeRemoved = 1;
                AmbaNetFifo_RemoveEnrty(&del_entry);
            }
            break;

        case AMBA_NETFIFO_EVENT_DATA_EOS:
            dbg_msg(" ----> AMBA_NETFIFO_EVENT_DATA_EOS!!\n");
            {
                int                             i;
                AMBA_NETFIFO_BITS_DESC_s        desc = {0};
                stream_fifo_dev_t               *pAct_vfifo_dev = NULL;

                desc.Type      = AMBA_FRAMEINFO_TYPE_DECODE_MARK;
                desc.Completed = 1;
                desc.Size      = AMBA_NETFIFO_MARK_EOS;

                // check fifo handler type
                pthread_mutex_lock(&pDev->mutex_stream_reader);
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( pDev->ppFifo_dev[i] &&
                        handler == pDev->ppFifo_dev[i]->vfifo_info.pFifo_handle )
                    {
                        pAct_vfifo_dev = pDev->ppFifo_dev[i];

                        if( pAct_vfifo_dev->vfifo_info.stream_type == STREAM_TYPE_LIVE &&
                            pDev->bAmbaNetfifo_Launched == true )
                        {
                            pDev->bAmbaNetfifo_Launched = false;
                            dbg_msg("ReportStatus(AMBA_NETFIFO_STATUS_END)\n");
                            AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_END);
                        }

                        if( pAct_vfifo_dev->pFifo_desc &&
                            pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame )
                        {
                            int                     rval = 0;
                            stream_fifo_param_t     fifo_param = {0};

                            fifo_param.u.queue_frm_arg.pDesc            = &desc;
                            fifo_param.u.queue_frm_arg.pNetfifo_handle  = handler;
                            rval = pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame(&pAct_vfifo_dev->vfifo_info, &fifo_param);
                            if( rval )
                            {
                            }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&pDev->mutex_stream_reader);
            }
            break;

        default:
            err_msg("unhandled event (%08x)\n", event);
            break;
    }

    return 0;
}

/**
 * Callback function for NetFiFo control event.
 */
static int
_cbCtrlEvent(unsigned int cmd, unsigned int param1, unsigned int param2, void *user_data)
{
    int                 result = 0;
    stream_dev_t        *pDev = (stream_dev_t*)user_data;

    if( pDev == NULL )
    {
        err_msg("Get NULL user_data !!\n");
        return -1;
    }

    switch( cmd )
    {
        case AMBA_NETFIFO_CMD_STARTENC:
            dbg_msg(" ----> RTOS Start Enc !!\n");
            {
                int                                 i, result = 0;
                stream_fifo_dev_t                   *pAct_vfifo_dev = NULL;
                AMBA_NETFIFO_MEDIA_STREAMID_LIST_s  stream_list = {0};
                AMBA_NETFIFO_MOVIE_INFO_CFG_s       movie_info = {.nTrack=0};

                result = AmbaNetFifo_GetMediaStreamIDList(&stream_list);
                if( result < 0 )
                {
                    err_msg("Fail to do AmbaNetFifo_GetMediaStreamIDList()\n");
                    break;
                }

                if( stream_list.Amount < 1 )
                {
                    err_msg("There is no valid stream. Maybe video record does not started yet.\n");
                    err_msg("%d, %x %x %x %x \n", stream_list.Amount,
                            stream_list.StreamID_List[0], stream_list.StreamID_List[1],
                            stream_list.StreamID_List[2], stream_list.StreamID_List[3]);
                    break;
                }

                result = AmbaNetFifo_GetMediaInfo(stream_list.StreamID_List[stream_list.Amount-1], &movie_info);
                if( result < 0 )
                {
                    err_msg("Fail to do AmbaNetFifo_GetMediaInfo()\n");
                    break;
                }

                if( movie_info.nTrack == 0 )
                {
                    err_msg("Media info NO tracks !!!\n");
                    break;
                }

                pthread_mutex_lock(&pDev->mutex_stream_reader);
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( !pDev->ppFifo_dev[i] )
                        continue;

                    pAct_vfifo_dev = pDev->ppFifo_dev[i];

                    err_msg("stream type = 0x%x\n", pAct_vfifo_dev->vfifo_info.stream_type);

                    if( pAct_vfifo_dev->vfifo_info.stream_type == STREAM_TYPE_LIVE )
                    {
                        AMBA_NETFIFO_MEDIA_TRACK_CFG_s      *pAct_track_info = NULL;
                        int                                 track_idx;
                        AMBA_NETFIFO_MEDIA_TRACK_TYPE_e     target_track_type = pAct_vfifo_dev->vfifo_info.track_type;

                        // check track exist or leave
                        for(track_idx = 0; track_idx < movie_info.nTrack; track_idx++)
                        {
                            err_msg("search track... %u\n", movie_info.Track[track_idx].nTrackType);
                            if( movie_info.Track[track_idx].nTrackType != target_track_type ||
                                movie_info.Track[track_idx].hCodec == NULL )
                            {
                                //err_msg("search track... %u\n", movie_info.Track[track_idx].nTrackType);
                                continue;
                            }
                            pAct_track_info = &movie_info.Track[track_idx];
                            break;
                        }

                        if( pAct_track_info == NULL )
                        {
                            result = -1;
                            break;
                        }

                        // re-create virtual fifo after change status of RTOS
                        if( pAct_vfifo_dev->pFifo_desc->vfifo_create )
                        {
                            int                     rval = 0;
                            stream_fifo_param_t     fifo_param = {0};

                            fifo_param.u.create_arg.cbEvent     = (unsigned int)pDev->event_handler;
                            fifo_param.u.create_arg.pTrack_info = pAct_track_info;
                            rval = pAct_vfifo_dev->pFifo_desc->vfifo_create(&pAct_vfifo_dev->vfifo_info, &fifo_param);
                            if( rval )
                            {
                                dbg_msg("create vfifo fail !!\n");
                                result = -1;
                                break;
                            }
                        }

                        pAct_vfifo_dev->fifo_reader.codec_id = pAct_vfifo_dev->vfifo_info.codec_id;
                        switch( pAct_vfifo_dev->fifo_reader.fifo_type )
                        {
                            case STREAM_TRACK_VIDEO:
                                pAct_vfifo_dev->fifo_reader.u.video.gop_size = pAct_vfifo_dev->vfifo_info.u.video.gop_size;
                                pAct_vfifo_dev->fifo_reader.u.video.width    = pAct_vfifo_dev->vfifo_info.u.video.width;
                                pAct_vfifo_dev->fifo_reader.u.video.height   = pAct_vfifo_dev->vfifo_info.u.video.height;
                                break;
                            case STREAM_TRACK_AUDIO:
                                pAct_vfifo_dev->fifo_reader.u.audio.sample_rate = pAct_vfifo_dev->vfifo_info.u.audio.sample_rate;
                                pAct_vfifo_dev->fifo_reader.u.audio.channels    = pAct_vfifo_dev->vfifo_info.u.audio.channels;
                                break;
                            default:
                                break;
                        }
                    }
                }

                if( result == 0 && pDev->bAmbaNetfifo_Launched == false )
                {
                    pDev->bAmbaNetfifo_Launched = true;
                    dbg_msg("ReportStatus(AMBA_NETFIFO_STATUS_START)\n");
                    AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_START);
                }

                pthread_mutex_unlock(&pDev->mutex_stream_reader);
            }
            break;
        case AMBA_NETFIFO_CMD_SWITCHENCSESSION:
            // TODO: it should skip EOS
            dbg_msg(" ----> RTOS  RTOS Switch enc session !!\n");
            {   // delete liveveiw vfifo without asking RTSP stop.
                int                             i;
                stream_fifo_dev_t               *pAct_vfifo_dev = NULL;

                // check fifo handler type
                pthread_mutex_lock(&pDev->mutex_stream_reader);
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( pDev->ppFifo_dev[i] &&
                        pDev->ppFifo_dev[i]->vfifo_info.pFifo_handle )
                    {
                        pAct_vfifo_dev = pDev->ppFifo_dev[i];

                        // destroy virtual fifo for change status of RTOS
                        if( pAct_vfifo_dev->vfifo_info.stream_type == STREAM_TYPE_LIVE &&
                            pAct_vfifo_dev->pFifo_desc->vfifo_destroy )
                        {
                            int                     rval = 0;
                            stream_fifo_param_t     fifo_param = {0};

                            rval = pAct_vfifo_dev->pFifo_desc->vfifo_destroy(&pAct_vfifo_dev->vfifo_info, &fifo_param);
                            if( rval )
                            {
                            }
                        }
                    }
                }

                if( pDev->bAmbaNetfifo_Launched == true )
                {
                    pDev->bAmbaNetfifo_Launched = false;
                    dbg_msg("ReportStatus(AMBA_NETFIFO_STATUS_SWITCHENCSESSION)\n");
                    AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_SWITCHENCSESSION);
                }

                pthread_mutex_unlock(&pDev->mutex_stream_reader);
            }
            break;
        case AMBA_NETFIFO_CMD_RELEASE:
            dbg_msg("----> RTOS Stop Enc, release!!\n");
        case AMBA_NETFIFO_CMD_STOPENC:
            dbg_msg(" ----> RTOS Stop Enc !!\n");
            {
                int                             i;
                AMBA_NETFIFO_BITS_DESC_s        desc = {0};
                stream_fifo_dev_t               *pAct_vfifo_dev = NULL;

                desc.Type      = AMBA_FRAMEINFO_TYPE_DECODE_MARK;
                desc.Completed = 1;
                desc.Size      = AMBA_NETFIFO_MARK_EOS;

                // check fifo handler type
                pthread_mutex_lock(&pDev->mutex_stream_reader);
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( pDev->ppFifo_dev[i] &&
                        //(pDev->ppFifo_dev[i]->vfifo_info.stream_type == STREAM_TYPE_LIVE) &&
                        pDev->ppFifo_dev[i]->vfifo_info.pFifo_handle )
                    {
                        pAct_vfifo_dev = pDev->ppFifo_dev[i];

                        if( pAct_vfifo_dev->vfifo_info.stream_type == STREAM_TYPE_LIVE &&
                            pDev->bAmbaNetfifo_Launched == true )
                        {
                            pDev->bAmbaNetfifo_Launched = false;
                            dbg_msg("ReportStatus(AMBA_NETFIFO_STATUS_END)\n");
                            AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_END);
                        }

                        if( pAct_vfifo_dev->pFifo_desc &&
                            pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame )
                        {
                            int                     rval = 0;
                            stream_fifo_param_t     fifo_param = {0};

                            fifo_param.u.queue_frm_arg.pDesc            = &desc;
                            rval = pAct_vfifo_dev->pFifo_desc->vfifo_queue_frame(&pAct_vfifo_dev->vfifo_info, &fifo_param);
                            if( rval )
                            {
                            }
                        }
                        break;
                    }
                }
                pthread_mutex_unlock(&pDev->mutex_stream_reader);
            }
            break;
        // case AMBA_NETFIFO_CMD_STARTNETPLAY:
        // case AMBA_NETFIFO_CMD_STOPNETPLAY:
        default:
            err_msg("unhandled event (%08x)\n", cmd);
            break;
    }

    return result;
}


/** frame buffer handle **/
static int
_get_frame_item_size(
    unsigned char   *w_ptr,
    unsigned char   *r_ptr,
    unsigned int    *pItem_size,
    unsigned int    *pIs_dummy_item)
{
    frame_item_hdr_t    *pFrame_info = (frame_item_hdr_t*)r_ptr;

    *pItem_size = pFrame_info->item_size;
    *pIs_dummy_item = (pFrame_info->item_size == sizeof(frame_item_hdr_t))
                    ? 1 : 0;
    return 0;
}

static int
_def_frame_buf_enable(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;

    pthread_mutex_lock(pMutex);
    do{
        if( pVfifo_info->pFrame_buf )
        {
            err_msg("frame buffer is already cteated !!\n");
            break;
        }

        if( !(pVfifo_info->pFrame_buf = malloc(pVfifo_info->frame_buf_size)) )
        {
            err_msg("malloc (track %u) frame buffer fail !\n", track_type);
            result = -1;
            break;
        }

        pVfifo_info->bBuf_pause = false;
        rb_opt_init(&pVfifo_info->rb_opt, pVfifo_info->pFrame_buf, pVfifo_info->frame_buf_size);

    }while(0);
    pthread_mutex_unlock(pMutex);

    return result;
}

static int
_def_frame_buf_disable(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    // AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;

    pthread_mutex_lock(pMutex);

    memset(&pVfifo_info->rb_opt, 0x0, sizeof(rb_operator_t));

    if( pVfifo_info->pFrame_buf )
    {
        free(pVfifo_info->pFrame_buf);
        pVfifo_info->pFrame_buf = NULL;
    }

    pthread_mutex_unlock(pMutex);

    return result;
}

static int
_def_frame_buf_queue_frame(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;
    AMBA_NETFIFO_BITS_DESC_s        *pBits_desc = NULL;

    pBits_desc = (AMBA_NETFIFO_BITS_DESC_s*)pfifo_param->u.queue_frm_arg.pDesc;

    pthread_mutex_lock(pMutex);
    do{
        stream_codec_param_t    codec_frm_param = {0};
        frame_item_hdr_t        item_hdr = {0};

        unsigned char       *pFrm_data[3] = {0};
        unsigned int        data_len[3] = {0};
        rb_w_data_info_t    w_data_info = {0};

        if( pVfifo_info->pFrame_buf == NULL )
        {
            break;
        }

        if( pBits_desc->Type == AMBA_FRAMEINFO_TYPE_DECODE_MARK ||
            pBits_desc->Type == AMBA_FRAMEINFO_TYPE_EOS ||
            pBits_desc->Size == AMBA_NETFIFO_MARK_EOS )
        {
            // handle EOS case
            int     dummy_frm_cnt = 3;

            item_hdr.frame_type         = (uint32_t)pBits_desc->Type;
            item_hdr.frame_size         = RB_INVALID_SIZE;
            item_hdr.item_size          = sizeof(frame_item_hdr_t);
            item_hdr.frame_start_offset = (uint32_t)(-1);

            pFrm_data[0] = (uint8_t*)&item_hdr;
            data_len[0]  = sizeof(frame_item_hdr_t);

            w_data_info.amount     = 1;
            w_data_info.ppData     = pFrm_data;
            w_data_info.pData_size = data_len;

            while( dummy_frm_cnt-- )
            {
                // send dummy frame
                result = rb_opt_update_w(&pVfifo_info->rb_opt, &w_data_info);
                if( result )
                {
                    err_msg("track 0x%x frame buf full, drop dummy frame !!\n", track_type);
                    break;
                }
            }
        }
        else
        {
            // handle normal case
            codec_frm_param.frame_type        = (AMBA_FRAMEINFO_TYPE_e)pBits_desc->Type;
            codec_frm_param.frame_num         = pBits_desc->SeqNum;
            codec_frm_param.pInput_base_addr  = pVfifo_info->pBufBase;
            codec_frm_param.pInput_limit_addr = pVfifo_info->pBufLimit;
            codec_frm_param.pInput_start_addr = pBits_desc->StartAddr;
            codec_frm_param.input_length      = pBits_desc->Size;

            if( pVfifo_info->bExist_specific_info == false &&
                pVfifo_info->pCodec_desc &&
                pVfifo_info->pCodec_desc->parse_specific_info )
            {
                int     rval = 0;

                rval = pVfifo_info->pCodec_desc->parse_specific_info(pVfifo_info, &codec_frm_param, NULL);
                if( rval < 0 )
                    break;
            }

            item_hdr.frame_num          = pBits_desc->SeqNum;
            item_hdr.pts                = pBits_desc->Pts;
            item_hdr.frame_type         = (uint32_t)pBits_desc->Type;
            item_hdr.frame_size         = pBits_desc->Size;
            item_hdr.item_size          = sizeof(frame_item_hdr_t) + item_hdr.frame_size;
            item_hdr.frame_start_offset = sizeof(frame_item_hdr_t);

            // copy to frame buffer (valid frame payload and size)
            pFrm_data[0] = (uint8_t*)&item_hdr;
            data_len[0]  = sizeof(frame_item_hdr_t);
            pFrm_data[1] = pBits_desc->StartAddr;
            data_len[1]  = pBits_desc->Size;

            w_data_info.amount     = 2;
            w_data_info.ppData     = pFrm_data;
            w_data_info.pData_size = data_len;

            if( pVfifo_info->pCodec_desc &&
                pVfifo_info->pCodec_desc->pre_proc_frame )
            {
                int     rval = 0;

                rval = pVfifo_info->pCodec_desc->pre_proc_frame(pVfifo_info, &codec_frm_param, NULL);
                if( rval < 0 )
                {
                    dbg_msg("handler=%p, frm type=%d, \n", pVfifo_info->pFifo_handle, codec_frm_param.frame_type);
                    break;
                }

                // Reflash frame payload addr/size
                pFrm_data[1] = codec_frm_param.pValidated_start_addr[0];
                data_len[1]  = codec_frm_param.validated_length[0];

                item_hdr.frame_size = codec_frm_param.validated_length[0];
                if( codec_frm_param.bWrap == true )
                {
                    w_data_info.amount = 3;
                    pFrm_data[2] = codec_frm_param.pValidated_start_addr[1];
                    data_len[2]  = codec_frm_param.validated_length[1];
                    item_hdr.frame_size += codec_frm_param.validated_length[1];
                }

                item_hdr.item_size = sizeof(frame_item_hdr_t) + item_hdr.frame_size;
            }

            result = rb_opt_update_w(&pVfifo_info->rb_opt, &w_data_info);
            if( result )
            {
                err_msg("track 0x%x frame buf full, drop frame !!\n", track_type);
            }
        }

        /* playback need to play every frame,
         * we use pause function to control remain size of frame buffer
         */
        if( pVfifo_info->stream_type == STREAM_TYPE_PLAYBACK &&
            pVfifo_info->bBuf_pause == false )
        {
            // check buffer space enough or not
            if( !rb_opt_confirm_space(&pVfifo_info->rb_opt, FRM_BUF_SIZE_TO_PAUSE(pVfifo_info->frame_buf_size)) )
            {
                int                           rval = 0;
                stream_reader_ctrl_box_t      ctrl_box = {0};

                ctrl_box.cmd  = STREAM_READER_CMD_PLAYBACK_PAUSE;
                rval = stream_cmd_control(pVfifo_info, &ctrl_box);
                if( rval )
                {
                    // TODO: error handle
                }
                pVfifo_info->bBuf_pause = true;
                break;
            }
        }
    }while(0);
    pthread_mutex_unlock(pMutex);

    return result;
}

static int
_def_frame_buf_peek_frame(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;
    stream_reader_frame_info_t      *pFrame_info = NULL;

    pFrame_info = (stream_reader_frame_info_t*)pfifo_param->u.get_frm_arg.pVfifo_frame_info;

    pthread_mutex_lock(pMutex);
    if( pVfifo_info->pFrame_buf )
    {
        frame_item_hdr_t    *pItem_hdr = NULL;
        uint32_t            item_size = 0;

        // frame should be free after RTSP send out !
        rb_opt_peek_r(&pVfifo_info->rb_opt,
                      RB_READ_TYPE_FETCH,
                      (uint8_t**)&pItem_hdr,
                      &item_size,
                      _get_frame_item_size);

        if( pFrame_info && item_size && pItem_hdr )
        {
            pFrame_info->pFrame_addr = (uint8_t*)((uint32_t)pItem_hdr + pItem_hdr->frame_start_offset);
            pFrame_info->frame_size  = pItem_hdr->frame_size;
            pFrame_info->pts         = pItem_hdr->pts;
            pFrame_info->frame_type  = pItem_hdr->frame_type;

            switch( track_type )
            {
                case AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO:
                    pFrame_info->u.audio.sample_rate = pVfifo_info->u.audio.sample_rate;
                    pFrame_info->u.audio.channels    = pVfifo_info->u.audio.channels;
                    break;
                case AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO:
                    pFrame_info->u.video.time_scale  = pVfifo_info->u.video.time_scale;
                    break;
                default:
                    break;
            }
        }
    }
    pthread_mutex_unlock(pMutex);

    return result;
}

static int
_def_frame_buf_get_frame(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;
    stream_reader_frame_info_t      *pFrame_info = NULL;

    pFrame_info = (stream_reader_frame_info_t*)pfifo_param->u.get_frm_arg.pVfifo_frame_info;

    pthread_mutex_lock(pMutex);
    if( pVfifo_info->pFrame_buf )
    {
        frame_item_hdr_t    *pItem_hdr = NULL;
        uint32_t            item_size = 0;

        // frame should be free after RTSP send out !
        rb_opt_update_r(&pVfifo_info->rb_opt,
                        RB_READ_TYPE_FETCH,
                        (uint8_t**)&pItem_hdr,
                        &item_size,
                        _get_frame_item_size);

        if( pFrame_info && item_size && pItem_hdr )
        {
            pFrame_info->frame_type  = pItem_hdr->frame_type;
            pFrame_info->frame_num   = pItem_hdr->frame_num;
            pFrame_info->pFrame_addr = (uint8_t*)((uint32_t)pItem_hdr + pItem_hdr->frame_start_offset);
            pFrame_info->frame_size  = pItem_hdr->frame_size;
            pFrame_info->pts         = pItem_hdr->pts;

            // handle EOS event
            if( item_size == RB_INVALID_SIZE )
            {
                pFrame_info->frame_type = AMBA_FRAMEINFO_TYPE_EOS;
            }

            switch( track_type )
            {
                case AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO:
                    pFrame_info->u.audio.sample_rate = pVfifo_info->u.audio.sample_rate;
                    pFrame_info->u.audio.channels    = pVfifo_info->u.audio.channels;
                    break;
                case AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO:
                    pFrame_info->u.video.time_scale  = pVfifo_info->u.video.time_scale;
                    break;
                default:
                    break;
            }
        }
    }

    pthread_mutex_unlock(pMutex);

    return result;
}

static int
_def_frame_buf_free_frame(
    vfifo_info_t        *pVfifo_info,
    stream_fifo_param_t *pfifo_param)
{
    int                             result = 0;
    pthread_mutex_t                 *pMutex = &pVfifo_info->mutex_vfifo;
    // AMBA_NETFIFO_MEDIA_TRACK_TYPE_e track_type = pVfifo_info->track_type;
    // stream_reader_frame_info_t      *pFrame_info = NULL;
    // pFrame_info = (stream_reader_frame_info_t*)pfifo_param->u.get_frm_arg.pVfifo_frame_info;

    pthread_mutex_lock(pMutex);

    if( pVfifo_info->pFrame_buf )
    {
        frame_item_hdr_t    *pItem_hdr = NULL;
        uint32_t            item_size = 0;

        rb_opt_update_r(&pVfifo_info->rb_opt,
                        RB_READ_TYPE_REMOVE,
                        (uint8_t**)&pItem_hdr,
                        &item_size,
                        _get_frame_item_size);
    }

    if( pVfifo_info->stream_type == STREAM_TYPE_PLAYBACK &&
        pVfifo_info->bBuf_pause == true )
    {
        // resume when helf frame buffer size be released.
        if( rb_opt_confirm_space(&pVfifo_info->rb_opt, FRM_BUF_SIZE_TO_RESUME(pVfifo_info->frame_buf_size)) )
        {
            int                           rval = 0;
            stream_reader_ctrl_box_t      ctrl_box = {0};

            ctrl_box.cmd  = STREAM_READER_CMD_PLAYBACK_RESUME;
            rval = stream_cmd_control(pVfifo_info, &ctrl_box);
            if( rval )
            {
                // TODO: error handle
            }
            pVfifo_info->bBuf_pause = false;
        }
    }

    pthread_mutex_unlock(pMutex);

    return result;
}
//=============================================================================
//                Public Function Definition
//=============================================================================
/**
 *  call once
 */
int
StreamReader_CreateHandle(
    stream_reader_t             **ppReader,
    stream_reader_init_info_t   *pInit_info)
{
    int             result = 0;
    stream_dev_t    *pDev = 0;

    _register_stream_desc();
    _register_stream_codec_desc();

    dbg_trace();

    do{
        int     rval = 0;

        if( *ppReader != 0 )
        {
            err_msg("error, Exist stream reader !!\n");
            break;
        }

        //----------------------
        // create device
        if( !(pDev = malloc(sizeof(stream_dev_t))) )
        {
            err_msg("malloc fail !\n");
            result = -1;
            break;
        }
        memset(pDev, 0x0, sizeof(stream_dev_t));

        //----------------------
        // init and set AmbaNetFifo module
        rval = AmbaNetFifo_init(&pDev->event_handler);
        if( rval < 0 )
        {
            err_msg("Fail to do AmbaNetFifo_init() !!\n");
            result = -2;
            break;
        }

        AmbaNetFifo_Reg_cbFifoEvent(_cbFrameReady, pDev);
        AmbaNetFifo_Reg_cbControlEvent(_cbCtrlEvent, pDev);

        //----------------------
        // create net fifo event process
        // TODO: how to check thread exist or not ??
        memset(&g_tid_netfifo, 0x0, sizeof(pthread_t));
        if( pthread_create(&g_tid_netfifo, NULL, (void *)&AmbaNetFifo_ExecEventProcess, NULL) != 0 )
        {
            err_msg("Fail to create AmbaNetFifo_ExecEventProcess thread !!\n");
            result = -3;
            break;
        }

        //----------------------
        // configure parameters
        if( pthread_mutex_init(&pDev->mutex_stream_reader, NULL) )
            err_msg("Fail to create stream reader mutex !\n");

        pDev->bAmbaNetfifo_Launched = false;
        //----------------------
        (*ppReader) = &pDev->stream_reader;

    }while(0);

    if( result != 0 )
    {
        stream_reader_t     *pReader = &pDev->stream_reader;
        StreamReader_DestroyHandle(&pReader);
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

int
StreamReader_DestroyHandle(
    stream_reader_t     **ppReader)
{
    int             result = 0;
    stream_dev_t    *pDev = STRUCTURE_POINTER(stream_dev_t, (*ppReader), stream_reader);
    dbg_trace();

    do{
        int     i;

        if( ppReader == 0 || *ppReader == 0 || pDev == 0 )
            break;

        pthread_mutex_lock(&pDev->mutex_stream_reader);

        // release fifo device
        for(i = 0; i < MAX_VFIFO_NUM; i++)
        {
            if( pDev->ppFifo_dev[i] == 0 )
                continue;

            // cbCtrlEven() should receive RELEASE event
            AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_END);
            break;
        }

        // TODO: need to sleep for cbCtrlEven ???
        AmbaNetFifo_StopEventProcess();
        pthread_join(g_tid_netfifo, NULL);

        AmbaNetFifo_release();

        for(i = 0; i < MAX_VFIFO_NUM; i++)
        {
            if( pDev->ppFifo_dev[i] == 0 )
                continue;

            free(pDev->ppFifo_dev[i]);
            pDev->ppFifo_dev[i] = NULL;
        }

        pthread_mutex_unlock(&pDev->mutex_stream_reader);
        pthread_mutex_destroy(&pDev->mutex_stream_reader);

        // release device
        if( pDev )
        {
            *ppReader = NULL;
            free(pDev);
        }
    }while(0);

    dbg_trace();
    return result;
}

/**
 *  open virtual fifo
 */
int
StreamReader_OpenVFifoReader(
    stream_reader_t             *pReader,
    stream_fifo_reader_init_t   *pFifo_init_info,
    stream_fifo_reader_t        **ppFifo_reader)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = NULL;

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_init_info, -1);
    _verify_handle(ppFifo_reader, -1);

    *ppFifo_reader = NULL;
    dbg_trace();

    do{
        AMBA_NETFIFO_MOVIE_INFO_CFG_s       movie_info;

        if( pFifo_init_info->fifo_cache_size == 0 )
        {
            err_msg("Wrong fifo cache size (%u) !!\n", pFifo_init_info->fifo_cache_size);
            break;
        }

        memset(&movie_info, 0x0, sizeof(AMBA_NETFIFO_MOVIE_INFO_CFG_s));
        //-------------------
        // get info from RTOS
        if( pFifo_init_info->stream_type == STREAM_TYPE_LIVE )
        {
            AMBA_NETFIFO_MEDIA_STREAMID_LIST_s  stream_list = {0};

            result = AmbaNetFifo_GetMediaStreamIDList(&stream_list);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaStreamIDList()\n");
                break;
            }

            if( stream_list.Amount < 1 )
            {
                err_msg("There is no valid stream. Maybe video record does not started yet.\n");
                err_msg("%d, %x %x %x %x \n", stream_list.Amount,
                        stream_list.StreamID_List[0],
                        stream_list.StreamID_List[1],
                        stream_list.StreamID_List[2],
                        stream_list.StreamID_List[3]);
                result = 1;
                break;
            }

            result = AmbaNetFifo_GetMediaInfo(stream_list.StreamID_List[stream_list.Amount-1], &movie_info);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaInfo()\n");
                break;
            }
        }
        else if ( pFifo_init_info->stream_type == STREAM_TYPE_PLAYBACK )
        {
            result = AmbaNetFifo_GetMediaInfo(pFifo_init_info->target_stream_id, &movie_info);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaInfo()\n");
                break;
            }
        }

        if( movie_info.nTrack )
        {
            int                                 i, track_idx;
            bool                                bVfifo_exist = false;
            AMBA_NETFIFO_MEDIA_TRACK_TYPE_e     target_track_type;
            AMBA_NETFIFO_MEDIA_TRACK_CFG_s      *pAct_track_info = NULL;

            switch( pFifo_init_info->fifo_type )
            {
                case STREAM_TRACK_VIDEO:    target_track_type = AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO; break;
                case STREAM_TRACK_AUDIO:    target_track_type = AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO; break;
                default:                    target_track_type = AMBA_NETFIFO_MEDIA_TRACK_TYPE_MAX;   break;
            }

            // check track exist or leave
            for(track_idx = 0; track_idx < movie_info.nTrack; track_idx++)
            {
                err_msg("search track... %u\n", movie_info.Track[track_idx].nTrackType);
                if( movie_info.Track[track_idx].nTrackType != target_track_type ||
                    movie_info.Track[track_idx].hCodec == NULL )
                {
                    //err_msg("search track... %u\n", movie_info.Track[track_idx].nTrackType);
                    continue;
                }
                pAct_track_info = &movie_info.Track[track_idx];
                break;
            }

            if( pAct_track_info == NULL )
            {
                err_msg("RTOS not support track %u (%u=video, %u=audio) !\n",
                        pFifo_init_info->fifo_type, STREAM_TRACK_VIDEO, STREAM_TRACK_AUDIO);
                result = -1;
                break;
            }

            // --------------------
            // Get a slot of fifo reader. If fifo reader already exist, use it.
            pthread_mutex_lock(&pDev->mutex_stream_reader);

            for(i = 0; i < MAX_VFIFO_NUM; i++)
            {
                if( pDev->ppFifo_dev[i] &&
                    pDev->ppFifo_dev[i]->vfifo_info.stream_type == pFifo_init_info->stream_type &&
                    pDev->ppFifo_dev[i]->fifo_reader.fifo_type == pFifo_init_info->fifo_type )
                {
                    pAct_fifo_dev = pDev->ppFifo_dev[i];
                    bVfifo_exist = true;
                    break;
                }
            }

            if( pAct_fifo_dev == NULL )
            {
                // get an empty slot
                for(i = 0; i < MAX_VFIFO_NUM; i++)
                {
                    if( pDev->ppFifo_dev[i] )
                        continue;

                    if( !(pAct_fifo_dev = malloc(sizeof(stream_fifo_dev_t))) )
                    {
                        err_msg("malloc fail !\n");
                        break;
                    }
                    memset(pAct_fifo_dev, 0x0, sizeof(stream_fifo_dev_t));
                    pAct_fifo_dev->order_index = i;

                    // --------------------
                    // find fifo description
                    pAct_fifo_dev->pFifo_desc = _find_stream_desc(pFifo_init_info->stream_type);
                    if( pAct_fifo_dev->pFifo_desc == 0 )
                    {
                        err_msg("Wrong stream type 0x%x !\n", pFifo_init_info->stream_type);
                        free(pAct_fifo_dev);
                        pAct_fifo_dev = NULL;
                        result = -3;
                        break;
                    }

                    pAct_fifo_dev->pFifo_desc->vfifo_enable      = _def_frame_buf_enable;
                    pAct_fifo_dev->pFifo_desc->vfifo_disable     = _def_frame_buf_disable;
                    pAct_fifo_dev->pFifo_desc->vfifo_queue_frame = _def_frame_buf_queue_frame;
                    pAct_fifo_dev->pFifo_desc->vfifo_peek_frame  = _def_frame_buf_peek_frame;
                    pAct_fifo_dev->pFifo_desc->vfifo_get_frame   = _def_frame_buf_get_frame;
                    pAct_fifo_dev->pFifo_desc->vfifo_free_frame  = _def_frame_buf_free_frame;

                    if( pthread_mutex_init(&pAct_fifo_dev->vfifo_info.mutex_vfifo, NULL) )
                        err_msg("Fail to create fifo mutex!\n");

                    // user re-define fifo descriptor if they want.
                    if( pFifo_init_info->cb_reset_fifo_descriptor )
                        pFifo_init_info->cb_reset_fifo_descriptor(pAct_fifo_dev->pFifo_desc, pFifo_init_info->pTunnel_info);

                    break;
                }

                if( pAct_fifo_dev == NULL )
                {
                    result = -2;
                    pthread_mutex_unlock(&pDev->mutex_stream_reader);
                    break;
                }

                // --------------------
                // setup fifo device info
                pAct_fifo_dev->vfifo_info.stream_type    = pFifo_init_info->stream_type;
                pAct_fifo_dev->vfifo_info.track_type     = target_track_type;
                pAct_fifo_dev->vfifo_info.frame_buf_size = pFifo_init_info->fifo_cache_size;
                pAct_fifo_dev->fifo_reader.fifo_type     = pFifo_init_info->fifo_type;
                pAct_fifo_dev->fifo_reader.stream_id     = (pFifo_init_info->stream_type == STREAM_TYPE_LIVE)
                                                         ? (uint32_t)-1 : pFifo_init_info->target_stream_id;

                pDev->ppFifo_dev[i] = pAct_fifo_dev;
            }

            // --------------------
            // create virtual fifo
            if( bVfifo_exist == false &&
                pAct_fifo_dev->pFifo_desc->vfifo_create )
            {
                int                     rval = 0;
                stream_fifo_param_t     fifo_param = {0};

                fifo_param.u.create_arg.cbEvent     = (unsigned int)pDev->event_handler;
                fifo_param.u.create_arg.pTrack_info = pAct_track_info;
                rval = pAct_fifo_dev->pFifo_desc->vfifo_create(&pAct_fifo_dev->vfifo_info, &fifo_param);
                if( rval )
                {
                    // TODO: error handle
                    result = -3;
                    pthread_mutex_unlock(&pDev->mutex_stream_reader);
                    break;
                }

                if( pDev->bAmbaNetfifo_Launched == false )
                {
                    // launch amba netfifo at RTOS
                    AmbaNetFifo_ReportStatus(AMBA_NETFIFO_STATUS_START);
                    pDev->bAmbaNetfifo_Launched = true;
                }

                pAct_fifo_dev->fifo_reader.codec_id = pAct_fifo_dev->vfifo_info.codec_id;
                switch( pAct_fifo_dev->fifo_reader.fifo_type )
                {
                    case STREAM_TRACK_VIDEO:
                        pAct_fifo_dev->fifo_reader.u.video.gop_size = pAct_fifo_dev->vfifo_info.u.video.gop_size;
                        pAct_fifo_dev->fifo_reader.u.video.width    = pAct_fifo_dev->vfifo_info.u.video.width;
                        pAct_fifo_dev->fifo_reader.u.video.height   = pAct_fifo_dev->vfifo_info.u.video.height;
                        break;
                    case STREAM_TRACK_AUDIO:
                        pAct_fifo_dev->fifo_reader.u.audio.sample_rate = pAct_fifo_dev->vfifo_info.u.audio.sample_rate;
                        pAct_fifo_dev->fifo_reader.u.audio.channels    = pAct_fifo_dev->vfifo_info.u.audio.channels;
                        break;
                }

                pAct_fifo_dev->vfifo_info.pCodec_desc = _find_stream_codec_desc(pAct_fifo_dev->vfifo_info.codec_id);
                if( pAct_fifo_dev->vfifo_info.pCodec_desc == 0 )
                {
                    err_msg("Can't find stream codec (0x%x) descriptor !!\n", pAct_fifo_dev->vfifo_info.codec_id);
                }

                // user re-define codec descriptor if they want.
                if( pFifo_init_info->cb_reset_codec_descriptor )
                {
                    pFifo_init_info->cb_reset_codec_descriptor(pAct_fifo_dev->vfifo_info.pCodec_desc, pFifo_init_info->pTunnel_info);
                }
            }

            pthread_mutex_unlock(&pDev->mutex_stream_reader);
            *ppFifo_reader = &pAct_fifo_dev->fifo_reader;
        }
    }while(0);

    if( result != 0 )
    {
        stream_fifo_reader_t    *pFifo_reader = &pAct_fifo_dev->fifo_reader;
        StreamReader_CloseVFifoReader(pReader, &pFifo_reader);
        err_msg("err %d !\n", result);
    }

    dbg_trace();
    return result;
}

/**
 *  close virtual fifo
 */
int
StreamReader_CloseVFifoReader(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    **ppFifo_reader)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, (*ppFifo_reader), fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(ppFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);

    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        int     i;

        // --------------------
        // find target fifo device
        for(i = 0; i < MAX_VFIFO_NUM; i++)
        {
            if( pDev->ppFifo_dev[i] == pAct_fifo_dev )
            {
                // TODO: how to sync GetFrame, How to handle mutex ???
                pDev->ppFifo_dev[i] = NULL;
                break;
            }
        }

        if( i == MAX_VFIFO_NUM )
        {
            dbg_msg("Can't find targeted fifo reader ! \n");
            break;
        }

        //------------------------
        // destroy fifo device
        if( pAct_fifo_dev->pFifo_desc )
        {
            int     rval = 0;

            // release frame buffer
            if( pAct_fifo_dev->pFifo_desc->vfifo_disable )
            {
                stream_fifo_param_t     fifo_param = {0};

                rval = pAct_fifo_dev->pFifo_desc->vfifo_disable(&pAct_fifo_dev->vfifo_info, &fifo_param);
                if( rval )
                {
                    // TODO: error handle
                }
            }

            // release codec specific info
            if( pAct_fifo_dev->vfifo_info.pCodec_desc &&
                pAct_fifo_dev->vfifo_info.pCodec_desc->destroy_specific_info )
            {
                stream_codec_param_t        codec_param = {0};
                pAct_fifo_dev->vfifo_info.pCodec_desc->destroy_specific_info(&pAct_fifo_dev->vfifo_info,
                                                                             &codec_param, NULL);
            }

            // destroy virtual fifo
            if( pAct_fifo_dev->pFifo_desc->vfifo_destroy )
            {
                stream_fifo_param_t     fifo_param = {0};

                rval = pAct_fifo_dev->pFifo_desc->vfifo_destroy(&pAct_fifo_dev->vfifo_info, &fifo_param);
                // TODO: error handle
            }
        }

        pAct_fifo_dev->order_index = (uint32_t)(-1);
        pthread_mutex_destroy(&pAct_fifo_dev->vfifo_info.mutex_vfifo);

        free(pAct_fifo_dev);

        *ppFifo_reader = NULL;
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }

    dbg_trace();
    return result;
}

/**
 * start to copy frames to frame buffer
 */
int
StreamReader_StartVFifo(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    *pFifo_reader)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);
    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->pFifo_desc &&
            pAct_fifo_dev->pFifo_desc->vfifo_enable )
        {
            int                     rval = 0;
            stream_fifo_param_t     fifo_param = {0};

            rval = pAct_fifo_dev->pFifo_desc->vfifo_enable(&pAct_fifo_dev->vfifo_info, &fifo_param);
            if( rval )
            {
                // TODO: error handle
            }
        }
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

/**
 * stop copy to frame buffer
 */
int
StreamReader_StopVFifo(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    *pFifo_reader)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);

    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->pFifo_desc &&
            pAct_fifo_dev->pFifo_desc->vfifo_disable )
        {
            int                     rval = 0;
            stream_fifo_param_t     fifo_param = {0};

            rval = pAct_fifo_dev->pFifo_desc->vfifo_disable(&pAct_fifo_dev->vfifo_info, &fifo_param);
            if( rval )
            {
                // TODO: error handle
            }
        }
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }

    dbg_trace();
    return result;
}

int
StreamReader_PeekFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);
    // _verify_handle(pFrame_info, -1);
    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->pFifo_desc &&
            pAct_fifo_dev->pFifo_desc->vfifo_get_frame )
        {
            int                     rval = 0;
            stream_fifo_param_t     fifo_param = {0};

            fifo_param.u.get_frm_arg.pVfifo_frame_info   = (void*)pFrame_info;
            rval = pAct_fifo_dev->pFifo_desc->vfifo_peek_frame(&pAct_fifo_dev->vfifo_info, &fifo_param);
            if( rval )
            {
                // TODO: error handle
            }
        }
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

int
StreamReader_GetFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);
    // _verify_handle(pFrame_info, -1);
    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->pFifo_desc &&
            pAct_fifo_dev->pFifo_desc->vfifo_get_frame )
        {
            int                     rval = 0;
            stream_fifo_param_t     fifo_param = {0};

            fifo_param.u.get_frm_arg.pVfifo_frame_info   = (void*)pFrame_info;
            rval = pAct_fifo_dev->pFifo_desc->vfifo_get_frame(&pAct_fifo_dev->vfifo_info, &fifo_param);
            if( rval )
            {
                // TODO: error handle
            }
        }
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

int
StreamReader_FreeFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);
    // _verify_handle(pFrame_info, -1);
    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);
    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->pFifo_desc &&
            pAct_fifo_dev->pFifo_desc->vfifo_free_frame )
        {
            int                     rval = 0;
            stream_fifo_param_t     fifo_param = {0};

            fifo_param.u.free_frm_arg.pVfifo_frame_info   = (void*)pFrame_info;
            rval = pAct_fifo_dev->pFifo_desc->vfifo_free_frame(&pAct_fifo_dev->vfifo_info, &fifo_param);
            if( rval )
            {
                // TODO: error handle
            }
        }
    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

int
StreamReader_GetSpecificInfo(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_specific_info_t      *pSpecific_info)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pFifo_reader, -1);
    _verify_handle(pAct_fifo_dev, -1);
    _verify_handle(pSpecific_info, -1);
    dbg_trace();

    pthread_mutex_lock(&pDev->mutex_stream_reader);

    do{
        // verify fifo device
        if( pAct_fifo_dev->order_index >= MAX_VFIFO_NUM ||
            pDev->ppFifo_dev[pAct_fifo_dev->order_index] != pAct_fifo_dev )
        {
            break;
        }

        if( pAct_fifo_dev->vfifo_info.codec_id != pSpecific_info->codec_id )
        {
            result = -2;
            err_msg("Codec not match (fifo: %u, request: %u) !!\n",
                    pAct_fifo_dev->vfifo_info.codec_id, pSpecific_info->codec_id);
            break;
        }

        switch( pSpecific_info->codec_id )
        {
            case AMP_FORMAT_MID_AVC:
            case AMP_FORMAT_MID_H264:
                pSpecific_info->u.h264.sps_length = pSpecific_info->u.h264.pps_length = 0;
                pSpecific_info->u.h264.pSPS_data  = pSpecific_info->u.h264.pPPS_data  = NULL;
                pSpecific_info->u.h264.width = pSpecific_info->u.h264.height = 0;

                if( pAct_fifo_dev->vfifo_info.stream_type == STREAM_TYPE_PLAYBACK )
                {
                    if( pAct_fifo_dev->vfifo_info.bExist_specific_info == false &&
                        pAct_fifo_dev->vfifo_info.pCodec_desc &&
                        pAct_fifo_dev->vfifo_info.pCodec_desc->parse_specific_info )
                    {
                        int                     rval = 0;
                        stream_codec_param_t    codec_frm_param = {0};

                        codec_frm_param.frame_type        = (AMBA_FRAMEINFO_TYPE_e)AMBA_FRAMEINFO_TYPE_IDR_FRAME;
                        codec_frm_param.pInput_base_addr  = NULL;
                        codec_frm_param.pInput_limit_addr = NULL;
                        codec_frm_param.pInput_start_addr = NULL;
                        codec_frm_param.input_length      = 0;

                        rval = pAct_fifo_dev->vfifo_info.pCodec_desc->parse_specific_info(&pAct_fifo_dev->vfifo_info,
                                                                                          &codec_frm_param, NULL);
                        if( rval < 0 )
                            break;
                    }
                }

                if( pAct_fifo_dev->vfifo_info.specific_info.pH264_info )
                {
                    stream_codec_h264_info_t    *pH264_info = pAct_fifo_dev->vfifo_info.specific_info.pH264_info;

                    pSpecific_info->u.h264.sps_length = pH264_info->sps_len;
                    pSpecific_info->u.h264.pSPS_data  = pH264_info->sps;
                    pSpecific_info->u.h264.pps_length = pH264_info->pps_len;
                    pSpecific_info->u.h264.pPPS_data  = pH264_info->pps;
                    pSpecific_info->u.h264.width      = pAct_fifo_dev->vfifo_info.u.video.width;
                    pSpecific_info->u.h264.height     = pAct_fifo_dev->vfifo_info.u.video.height;
                }
                break;

            case AMP_FORMAT_MID_AAC:
                break;

            default:
                err_msg("Not support CODEC %u !!\n", pSpecific_info->codec_id);
                break;
        }

    }while(0);

    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }
    dbg_trace();
    return result;
}

int
StreamReader_Control(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_ctrl_box_t    *pCtrl_box)
{
    int                 result = 0;
    stream_dev_t        *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);
    stream_fifo_dev_t   *pAct_fifo_dev = STRUCTURE_POINTER(stream_fifo_dev_t, pFifo_reader, fifo_reader);

    _verify_handle(pReader, -1);
    _verify_handle(pCtrl_box, -1);

    pAct_fifo_dev = (pFifo_reader) ? pAct_fifo_dev : NULL;

    pthread_mutex_lock(&pDev->mutex_stream_reader);

    do{
        int                     i;
        stream_status_info_t    *pCur_status_info = NULL;
        vfifo_info_t            *pVfifo_info = NULL;

        if( pAct_fifo_dev )
            pVfifo_info = &pAct_fifo_dev->vfifo_info;

        if( pCtrl_box->stream_type == STREAM_TYPE_PLAYBACK )
        {
            // playback
            if( pCtrl_box->stream_tag == 0 )
            {
                dbg_msg("unknow stream tag, do nothing !\n");
                break;
            }

            for(i = 0; i < STREAM_PLAYBACK_STATUS_NUM; i++)
            {
                if( pReader->playback_status[i].stream_tag
                        == pCtrl_box->stream_tag )
                {
                    pCur_status_info = &pReader->playback_status[i];
                    break;
                }
            }

            if( pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_OPEN )
            {
                // skip if has be opened.
                if( pCur_status_info )
                {
                    pCtrl_box->out.pb_open.stream_id = pCur_status_info->target_stream_id;
                    break;
                }

                // find an empty slot
                for(i = 0; i < STREAM_PLAYBACK_STATUS_NUM; i++)
                {
                    if( pReader->playback_status[i].stream_tag == 0 )
                    {
                        pCur_status_info = &pReader->playback_status[i];
                        break;
                    }
                }
            }

            if( pCur_status_info == NULL ||
                (pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_PAUSE && pCur_status_info->state == STREAM_STATE_PAUSE) ||
                (pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_PLAY && pCur_status_info->state == STREAM_STATE_PLAYING) ||
                (pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_RESUME && pCur_status_info->state == STREAM_STATE_PLAYING) )
            {
                break;
            }
        }
        else
        {
            // live view
            if( pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_OPEN ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_PLAY ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_STOP ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_RESET ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_PAUSE ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_RESUME ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_CONFIG ||
                pCtrl_box->cmd == STREAM_READER_CMD_PLAYBACK_GET_DURATION )
            {
                break;
            }

            pCur_status_info = &pReader->live_stream_status;
        }

        result = stream_cmd_control(pVfifo_info, pCtrl_box);
        if( result )
        {
            // TODO: error handle
            break;
        }

        // live view update status
        if( pCtrl_box->stream_type == STREAM_TYPE_LIVE )
        {
            if( pCtrl_box->cmd == STREAM_READER_CMD_SEND_RR_STAT )
                pCur_status_info->extra_data = pCtrl_box->pExtra_data;
            break;
        }

        // update playback status info
        switch( pCtrl_box->cmd )
        {
            case STREAM_READER_CMD_PLAYBACK_OPEN:
                pCur_status_info->stream_tag       = pCtrl_box->stream_tag;
                pCur_status_info->target_stream_id = pCtrl_box->out.pb_open.stream_id;
                pCur_status_info->state            = STREAM_STATE_OPEN;
                pCur_status_info->extra_data       = pCtrl_box->pExtra_data;

                pReader->playback_cnt++;
                break;

            case STREAM_READER_CMD_PLAYBACK_RESUME:
            case STREAM_READER_CMD_PLAYBACK_PLAY:
                pCur_status_info->state = STREAM_STATE_PLAYING;
                break;

            case STREAM_READER_CMD_PLAYBACK_STOP:
                pCur_status_info->stream_tag       = 0;
                pCur_status_info->target_stream_id = 0;
                pCur_status_info->state            = STREAM_STATE_UNKNOW;

                pReader->playback_cnt--;
                break;

            case STREAM_READER_CMD_PLAYBACK_PAUSE:
                pCur_status_info->state = STREAM_STATE_PAUSE;
                break;

            default:
                break;
        }
    }while(0);
    pthread_mutex_unlock(&pDev->mutex_stream_reader);

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }

    return result;
}



/*
int
StreamReader_tamplete(
    stream_reader_t     *pReader,
    void                *extraData)
{
    int             result = 0;
    stream_dev_t    *pDev = STRUCTURE_POINTER(stream_dev_t, pReader, stream_reader);

    _verify_handle(pDev, -1);
    // TODO: Need to mutex for pReader

    if( result != 0 )
    {
        err_msg("err 0x%x !\n", result);
    }

    return result;
}*/

