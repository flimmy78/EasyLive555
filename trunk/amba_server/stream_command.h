#ifndef __stream_command_H_wDczxG7I_lOSi_HH42_s7rr_uq4u4rpckPuu__
#define __stream_command_H_wDczxG7I_lOSi_HH42_s7rr_uq4u4rpckPuu__

#ifdef __cplusplus
extern "C" {
#endif

#include "AmbaNetFifo.h"
#include "stream_fifo.h"
//=============================================================================
//                Constant Definition
//=============================================================================
#define MAX_CMD_PARAM_SIZE      128

/**
 *  stream reader control command
 *  map to enum _AMP_NETFIFO_PLAYBACK_OP_e_
 *      at ~/rtos/vendors/ambarella/inc/mw/net/NetFifo.h
 */
typedef enum stream_reader_cmd
{
    STREAM_READER_CMD_PLAYBACK_OPEN = 1,              /**< open file for playback. */
    STREAM_READER_CMD_PLAYBACK_PLAY,                  /**< start playback. */
    STREAM_READER_CMD_PLAYBACK_STOP,                  /**< stop playback */
    STREAM_READER_CMD_PLAYBACK_RESET,                 /**< reset playback */
    STREAM_READER_CMD_PLAYBACK_PAUSE,                 /**< pause playback */
    STREAM_READER_CMD_PLAYBACK_RESUME,                /**< resume playback */
    STREAM_READER_CMD_PLAYBACK_CONFIG,                /**< enabling playback stream */
    STREAM_READER_CMD_PLAYBACK_GET_VID_FTIME,         /**< retrieve video time per frame */
    STREAM_READER_CMD_PLAYBACK_GET_VID_TICK,          /**< retrieve video tick per frame */
    STREAM_READER_CMD_PLAYBACK_GET_AUD_FTIME,         /**< retrieve audio time per frame */
    STREAM_READER_CMD_PLAYBACK_GET_DURATION,          /**< retrieve clip duration */
    STREAM_READER_CMD_SET_LIVE_BITRATE,               /**< set AVG bitrate */
    STREAM_READER_CMD_GET_LIVE_BITRATE,               /**< get latest reported bitrate from BRC */
    STREAM_READER_CMD_GET_LIVE_AVG_BITRATE,           /**< get sensor setting */
    STREAM_READER_CMD_SET_NET_BANDWIDTH,              /**< set bandwidth for BRC callback */
    STREAM_READER_CMD_SEND_RR_STAT,                   /**< send rr report */
    STREAM_READER_CMD_SET_PARAMETER,                  /**< send RTSP extend field */
    STREAM_READER_CMD_PLAYBACK_GET_SPS_PPS            /**< get sps&pps of the specifid file or playback file */
} stream_reader_cmd_t;
//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  stream reader control box
 */
typedef struct stream_reader_ctrl_box
{
    stream_reader_cmd_t     cmd;
    uint32_t                stream_tag;
    uint32_t                stream_type;
    void                    *pExtra_data;

    union {
        // playback open
        struct {
            char            *pPath;
        } pb_open;

        // playback play
        struct {
            int32_t         seek_time;
        } pb_play;

        // send receive report stat
        struct {
            uint32_t    fraction_lost;
            uint32_t    jitter;
            double      propagation_delay; // round-trip propagation delay
        } send_rr_stat;

        // set bit rate
        struct {
            int         new_bitrate;
        } set_bitrate;

        // get sps/pps
        struct {
            char            *pPath;
        } get_sps_pps;

    } in;

    union {
        // playback open
        struct {
            uint32_t        stream_id;
        } pb_open;

        // get sps/pps
        struct {
            stream_codec_h264_info_t    *pH264_info;
        } get_sps_pps;

        // get frame tick
        struct {
            uint32_t    tick_pre_frame;
        } get_vid_frm_tick;

        // get frame time
        struct {
            uint32_t    frame_time;
        } get_vid_frm_time;

        // get live bit-rate
        struct {
            int         bit_rate;
        } get_live_bitrate;

        // get average bit-rate
        struct {
            int         bit_rate;
        } get_avg_bitrate;

        // get duration
        struct {
            uint32_t    duration;
        } get_duration;

    } out;

} stream_reader_ctrl_box_t;

/**
 *  analyze command response
 */
typedef int (*FUNC_CMD_RECEIVER)(vfifo_info_t *pFifo_info, AMBA_NETFIFO_PLAYBACK_OP_PARAM_s *pReq_param,
                                 stream_reader_ctrl_box_t *pCtrl_box);
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
stream_cmd_control(
    vfifo_info_t                *pVfifo_info,
    stream_reader_ctrl_box_t    *pCtrl_box);


#ifdef __cplusplus
}
#endif

#endif
