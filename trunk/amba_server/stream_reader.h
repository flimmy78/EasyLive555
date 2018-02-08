#ifndef __amba_stream_reader_H_wFk2t49u_liQL_H2HJ_sXcL_uhvFyghhvn7m__
#define __amba_stream_reader_H_wFk2t49u_liQL_H2HJ_sXcL_uhvFyghhvn7m__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "stdbool.h"
#include "stream_fifo.h"
#include "stream_command.h"
//=============================================================================
//                Constant Definition
//=============================================================================
/**
 *  Now, only support playback 1 file !
 *  It MUST be (STREAM_PLAYBACK_STATUS_NUM == 1)
 */
#define STREAM_PLAYBACK_STATUS_NUM      1

typedef enum stream_type
{
    STREAM_TYPE_LIVE    = 0xa0,     // view finder
    STREAM_TYPE_PLAYBACK,           // playback file

} stream_type_t;

typedef enum stream_track_type
{
    STREAM_TRACK_VIDEO    = 0xd0,
    STREAM_TRACK_AUDIO,

} stream_track_type_t;

typedef enum stream_state
{
    STREAM_STATE_UNKNOW =   0,
    STREAM_STATE_OPEN,
    STREAM_STATE_PLAYING,
    STREAM_STATE_PAUSE,
    STREAM_STATE_STOP,

} stream_state_t;
//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  stream reader init info
 */
typedef struct stream_reader_init_info
{
    uint32_t    reserved;

} stream_reader_init_info_t;

/**
 *  init info of virtual fifo
 */
typedef struct stream_fifo_reader_init
{
    stream_type_t       stream_type;
    stream_track_type_t fifo_type;
    // AMP_FORMAT_MID_e    codec_id;  // TODO: maybe need to multi sound track, not ready

    uint32_t            fifo_cache_size;

    // for playback
    uint32_t            target_stream_id; // select the target file stream from RTOS

    // user re-define fifo descriptor
    void    *pTunnel_info;
    int     (*cb_reset_fifo_descriptor)(stream_fifo_desc_t *pFifo_desc, void *pTunnel_info);
    int     (*cb_reset_codec_descriptor)(stream_codec_desc_t *pCodec_desc, void *pTunnel_info);

} stream_fifo_reader_init_t;

/**
 *  stream reader frame info
 */
typedef struct stream_reader_frame_info
{
    unsigned long long  pts;
    uint8_t     *pFrame_addr;
    uint32_t    frame_size;
    uint32_t    frame_type;
    uint32_t    frame_num;

    union {
        struct {
            uint32_t    sample_rate;
            uint32_t    channels;
        } audio;

        struct {
            uint32_t    time_scale;
        } video;
    } u;

} stream_reader_frame_info_t;

/**
 * stream specific info
 * e.g. SPS/PPS
 */
typedef struct stream_specific_info
{
    AMP_FORMAT_MID_e       codec_id;

    union {
        // h264
        struct {
            uint32_t        sps_length;
            uint8_t         *pSPS_data;
            uint32_t        pps_length;
            uint8_t         *pPPS_data;
            uint32_t        width;
            uint32_t        height;
        } h264;
    } u;

} stream_specific_info_t;

/**
 * stream fifo reader
 */
typedef struct stream_fifo_reader
{
    stream_track_type_t     fifo_type;
    AMP_FORMAT_MID_e        codec_id;
    uint32_t                stream_id;

    union {
        struct {
            uint32_t    sample_rate;
            uint32_t    channels;
        } audio;

        struct {
            uint32_t    gop_size;
            uint32_t    width;
            uint32_t    height;
        } video;
    } u;

} stream_fifo_reader_t;

/**
 *  report stream status
 */
typedef struct stream_status_info
{
    uint32_t        stream_tag; // CRC32(path)
    uint32_t        target_stream_id;
    stream_state_t  state;
    void            *extra_data;

} stream_status_info_t;

/**
 * stream reader
 */
typedef struct stream_reader
{
    //stream_status_info_t     liveview_status;

    uint32_t                 playback_cnt;
    stream_status_info_t     live_stream_status;
    stream_status_info_t     playback_status[STREAM_PLAYBACK_STATUS_NUM];

} stream_reader_t;
//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================

//=============================================================================
//                Public Function Definition
//=============================================================================
int
StreamReader_CreateHandle(
    stream_reader_t             **ppReader,
    stream_reader_init_info_t   *pInit_info);

int
StreamReader_DestroyHandle(
    stream_reader_t     **ppReader);

/**
 *  open virtual fifo
 */
int
StreamReader_OpenVFifoReader(
    stream_reader_t             *pReader,
    stream_fifo_reader_init_t   *pVfifo_info,
    stream_fifo_reader_t        **ppFifo_reader);

/**
 *  close virtual fifo
 */
int
StreamReader_CloseVFifoReader(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    **ppFifo_reader);

/**
 *  enable frame buffer queue
 */
int
StreamReader_StartVFifo(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    *pFifo_reader);

/**
 *  disable frame buffer queue
 */
int
StreamReader_StopVFifo(
    stream_reader_t         *pReader,
    stream_fifo_reader_t    *pFifo_reader);

/**
 *  peek a frame info from frame buffer
 */
int
StreamReader_PeekFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info);

/**
 *  fetch a frame info from frame buffer
 */
int
StreamReader_GetFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info);

/**
 *  free a frame in frame buffer
 *  This function MUST be after Get Frame.
 */
int
StreamReader_FreeFrame(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_frame_info_t  *pFrame_info);

/**
 *  get stream codec's specific info
 */
int
StreamReader_GetSpecificInfo(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_specific_info_t      *pSpecific_info);

/**
 *  control stream behavior of RTOS
 */
int
StreamReader_Control(
    stream_reader_t             *pReader,
    stream_fifo_reader_t        *pFifo_reader,
    stream_reader_ctrl_box_t    *pCtrl_box);


#ifdef __cplusplus
}
#endif

#endif
