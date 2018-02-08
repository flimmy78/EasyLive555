#ifndef __stream_fifo_H_wxenCNdl_lyb0_HYAt_sN8k_usaVyAtcLFSM__
#define __stream_fifo_H_wxenCNdl_lyb0_HYAt_sN8k_usaVyAtcLFSM__

#ifdef __cplusplus
extern "C" {
#endif


#include "pthread.h"
#include "ring_frame_buf_opt.h"
#include "AmbaNetFifo.h"
#include "stream_codec.h"
//=============================================================================
//                Constant Definition
//=============================================================================

//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 * virtual fifo info
 */
typedef struct vfifo_info
{
    unsigned int        stream_type; // ref stream_type_t

    pthread_mutex_t     mutex_vfifo;

    void                *pPriv_info;

    AMBA_NETFIFO_MEDIA_TRACK_TYPE_e  track_type;
    AMBA_NETFIFO_HDLR_s              *pFifo_handle;
    void                             *pBufBase;
    void                             *pBufLimit;

    // ring frame buffer operator
    rb_operator_t       rb_opt;
    unsigned char       *pFrame_buf;
    unsigned int        frame_buf_size;

    // media info
    AMP_FORMAT_MID_e    codec_id;

    union {
        struct {
            unsigned int    time_scale;
            unsigned int    time_per_frame;
            unsigned int    init_delay;
            unsigned int    gop_size;
            unsigned int    width;
            unsigned int    height;
        } video;

        struct {
            unsigned int    sample_rate;
            unsigned int    channels;
            unsigned int    bits_per_sample;
        } audio;
    } u;

    // codec frame process
    stream_codec_desc_t     *pCodec_desc;
    uint32_t                bExist_specific_info;

    union {
        stream_codec_h264_info_t    *pH264_info;
    } specific_info;

    // frame process
    uint32_t                bBuf_pause;

} vfifo_info_t;

/**
 * fifo parameters
 */
typedef struct stream_fifo_param
{
    void            *pExtra_data;

    union {
        // vfifo_create
        struct {
            unsigned int                        cbEvent;
            AMBA_NETFIFO_MEDIA_TRACK_CFG_s      *pTrack_info;
        } create_arg;

        // vfifo_destroy
        struct {
            unsigned int        reserved;
        } destroy_arg;

        // vfifo_enable
        struct {
            unsigned int        reserved;
        } enable_arg;

        // vfifo_disable
        struct {
            unsigned int        reserved;
        } disable_arg;

        // vfifo_queue_frame
        struct {
            AMBA_NETFIFO_BITS_DESC_s    *pDesc;
            AMBA_NETFIFO_HDLR_s         *pNetfifo_handle;
        } queue_frm_arg;

        // vfifo_get_frame
        struct {
            void            *pVfifo_frame_info;
        } get_frm_arg;

        // vfifo_free_frame
        struct {
            void            *pVfifo_frame_info;
        } free_frm_arg;

    } u;

} stream_fifo_param_t;

/**
 * fifo descriptor
 */
typedef struct stream_fifo_desc
{
    struct stream_fifo_desc  *next;

    const char          *name;
    unsigned int        stream_type;

    int     (*vfifo_create)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_destroy)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);

    int     (*vfifo_enable)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_disable)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_queue_frame)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_peek_frame)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_get_frame)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);
    int     (*vfifo_free_frame)(vfifo_info_t *pVfifo_info, stream_fifo_param_t *pfifo_info);

} stream_fifo_desc_t;

//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================

//=============================================================================
//                Public Function Definition
//=============================================================================

#ifdef __cplusplus
}
#endif

#endif
