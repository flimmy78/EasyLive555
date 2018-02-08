
#include "util_def.h"
#include "stream_command.h"
#include "stream_reader.h"
//=============================================================================
//                Constant Definition
//=============================================================================

//=============================================================================
//                Macro Definition
//=============================================================================
#define _to_string(a)       #a
//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  ref to ~\rtos\app\connected\applib\inc\net\ApplibNet_Fifo.h
 */
typedef struct _APPLIB_NETFIFO_SPS_PPS_s_
{
    uint8_t     Sps[64];
    uint32_t    SpsLen;
    uint8_t     Pps[32];
    uint8_t     PpsLen;
} APPLIB_NETFIFO_SPS_PPS_s;
//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================

//=============================================================================
//                Public Function Definition
//=============================================================================
#if 0
static void
_stream_cmd_log(
    vfifo_info_t            *pVfifo_info,
    stream_reader_cmd_t     cmd_id)
{
    char    *cmd_name[] = {
        _to_string(STREAM_READER_CMD_PLAYBACK_OPEN),
        _to_string(STREAM_READER_CMD_PLAYBACK_PLAY),
        _to_string(STREAM_READER_CMD_PLAYBACK_STOP),
        _to_string(STREAM_READER_CMD_PLAYBACK_RESET),
        _to_string(STREAM_READER_CMD_PLAYBACK_PAUSE),
        _to_string(STREAM_READER_CMD_PLAYBACK_RESUME),
        _to_string(STREAM_READER_CMD_PLAYBACK_CONFIG),
        _to_string(STREAM_READER_CMD_PLAYBACK_GET_VID_FTIME),
        _to_string(STREAM_READER_CMD_PLAYBACK_GET_VID_TICK),
        _to_string(STREAM_READER_CMD_PLAYBACK_GET_AUD_FTIME),
        _to_string(STREAM_READER_CMD_PLAYBACK_GET_DURATION),
        _to_string(STREAM_READER_CMD_SET_LIVE_BITRATE),
        _to_string(STREAM_READER_CMD_GET_LIVE_BITRATE),
        _to_string(STREAM_READER_CMD_GET_LIVE_AVG_BITRATE),
        _to_string(STREAM_READER_CMD_SET_NET_BANDWIDTH),
        _to_string(STREAM_READER_CMD_SEND_RR_STAT),
        _to_string(STREAM_READER_CMD_SET_PARAMETER),
        _to_string(STREAM_READER_CMD_PLAYBACK_GET_SPS_PPS)
    };

    if( cmd_id > STREAM_READER_CMD_PLAYBACK_GET_SPS_PPS )
        err_msg("unknow cmd_id 0x%x\n", cmd_id);
    else
        err_msg("cmd_id= %s\n", cmd_name[cmd_id - STREAM_READER_CMD_PLAYBACK_OPEN]);
    return;
}

static int
_stream_cmd_rx_default(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    return 0;
}
#else
    #define _stream_cmd_log(a, b)
#endif

static int
_stream_cmd_rx_get_vid_frm_tick(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.get_vid_frm_tick.tick_pre_frame = pResp_param->OP;
    return 0;
}

static int
_stream_cmd_rx_get_vid_frm_time(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.get_vid_frm_time.frame_time = pResp_param->OP;
    return 0;
}

static int
_stream_cmd_rx_get_duration(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.get_duration.duration = pResp_param->OP;
    return 0;
}

static int
_stream_cmd_rx_live_bitrate(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.get_live_bitrate.bit_rate = pResp_param->OP;
    return 0;
}

static int
_stream_cmd_rx_average_bitrate(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.get_avg_bitrate.bit_rate = pResp_param->OP;
    return 0;
}

static int
_stream_cmd_rx_pb_open(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    pCtrl_box->out.pb_open.stream_id = pResp_param->OP;
    return (pResp_param->OP < 0) ? -1 : 0;
}


static int
_stream_cmd_rx_get_sps_pps(
    vfifo_info_t                        *pFifo_info,
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    *pResp_param,
    stream_reader_ctrl_box_t            *pCtrl_box)
{
    int     ret= -1;

    if( pCtrl_box->out.get_sps_pps.pH264_info && pResp_param->OP == 0 )
    {
        APPLIB_NETFIFO_SPS_PPS_s    *pNetfifo_sps_pps = NULL;
        stream_codec_h264_info_t    *pH264_info = NULL;
        trace();

        pNetfifo_sps_pps = (APPLIB_NETFIFO_SPS_PPS_s*)pResp_param->Param;
        pH264_info       = pCtrl_box->out.get_sps_pps.pH264_info;

        pH264_info->sps_len = pNetfifo_sps_pps->SpsLen;
        memcpy(pH264_info->sps, pNetfifo_sps_pps->Sps, pNetfifo_sps_pps->SpsLen);

        pH264_info->pps_len = pNetfifo_sps_pps->PpsLen;
        memcpy(pH264_info->pps, pNetfifo_sps_pps->Pps, pNetfifo_sps_pps->PpsLen);
        ret = 0;
    }
    return ret;
}

int
stream_cmd_control(
    vfifo_info_t                *pVfifo_info,
    stream_reader_ctrl_box_t    *pCtrl_box)
{
    int     result = 0;
    int     bSkip_cmd = false;

    FUNC_CMD_RECEIVER                   cmd_receiver = NULL;
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_in = {0,{0}};
    AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_out = {0,{0}};

    param_in.OP = pCtrl_box->cmd;

    _stream_cmd_log(pVfifo_info, pCtrl_box->cmd);
    switch( pCtrl_box->cmd )
    {
        case STREAM_READER_CMD_PLAYBACK_OPEN:
            // dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            if( pCtrl_box->in.pb_open.pPath == NULL )
            {
                err_msg("Invalid playback file path !!\n");
                bSkip_cmd = true;
                break;
            }

            cmd_receiver = _stream_cmd_rx_pb_open;
            snprintf((char*)param_in.Param, MAX_CMD_PARAM_SIZE, "%s", pCtrl_box->in.pb_open.pPath);
            break;

        case STREAM_READER_CMD_PLAYBACK_GET_SPS_PPS:
            if( pCtrl_box->in.get_sps_pps.pPath == NULL )
            {
                // no filename, retrieve current playback file
                param_in.Param[0] = '\0';
            }
            else
            {
                snprintf((char*)param_in.Param, MAX_CMD_PARAM_SIZE, "%s", pCtrl_box->in.get_sps_pps.pPath);
            }

            cmd_receiver = _stream_cmd_rx_get_sps_pps;
            break;

        case STREAM_READER_CMD_PLAYBACK_PLAY:
            // dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            if( pCtrl_box->in.pb_play.seek_time )
            {
                int     *pSeek_time = (int*)&param_in.Param[0];
                *pSeek_time = pCtrl_box->in.pb_play.seek_time;
            }
            break;
        case STREAM_READER_CMD_PLAYBACK_GET_VID_FTIME:
            //dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            cmd_receiver = _stream_cmd_rx_get_vid_frm_time;
            break;
        case STREAM_READER_CMD_PLAYBACK_GET_VID_TICK:
            //dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            cmd_receiver = _stream_cmd_rx_get_vid_frm_tick;
            break;
        case STREAM_READER_CMD_PLAYBACK_GET_DURATION:
            //dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            cmd_receiver = _stream_cmd_rx_get_duration;
            break;
        case STREAM_READER_CMD_SEND_RR_STAT:
            //dbg_msg("----- %d, %d, %llu\n", pCtrl_box->in.send_rr_stat.fraction_lost,
            //            pCtrl_box->in.send_rr_stat.jitter, pCtrl_box->in.send_rr_stat.propagation_delay);
            memcpy(param_in.Param, &pCtrl_box->in.send_rr_stat, sizeof(pCtrl_box->in.send_rr_stat));
            break;
        case STREAM_READER_CMD_SET_LIVE_BITRATE:
            if( pCtrl_box->in.set_bitrate.new_bitrate )
            {
                int     *pBitrate = (int*)&param_in.Param[0];
                // err_msg("--------- SET_LIVE_BITRATE bitrate =%d\n", pCtrl_box->in.set_bitrate.new_bitrate);
                *pBitrate = pCtrl_box->in.set_bitrate.new_bitrate;
            }
            break;
        case STREAM_READER_CMD_GET_LIVE_BITRATE:
            cmd_receiver = _stream_cmd_rx_live_bitrate;
            break;
        case STREAM_READER_CMD_GET_LIVE_AVG_BITRATE:
            cmd_receiver = _stream_cmd_rx_average_bitrate;
            break;

        case STREAM_READER_CMD_PLAYBACK_STOP:
        case STREAM_READER_CMD_PLAYBACK_RESET:
        case STREAM_READER_CMD_PLAYBACK_PAUSE:
        case STREAM_READER_CMD_PLAYBACK_RESUME:
        case STREAM_READER_CMD_PLAYBACK_CONFIG:
        case STREAM_READER_CMD_PLAYBACK_GET_AUD_FTIME:
        case STREAM_READER_CMD_SET_NET_BANDWIDTH:
        case STREAM_READER_CMD_SET_PARAMETER:
            /* don't need input parameters */
            // dbg_msg("---------id=%p cmd =0x%x\n", pVfifo_info, pCtrl_box->cmd);
            break;
        default:
            bSkip_cmd = true;
            err_msg("fifo(%p) not support command 0x%x !\n", pVfifo_info, pCtrl_box->cmd);
            break;
    }

    if( bSkip_cmd == false )
    {
        result = AmbaNetFifo_PlayBack_OP(&param_in, &param_out);
        if( result < 0 )
        {
            err_msg("fail to do AmbaNetFifo_PlayBack_OP(0x%08x), %d\n", pCtrl_box->cmd, result);
            return result;
        }

        if( cmd_receiver )
        {
            vfifo_info_t            *pVfifo_info = NULL;

            /* Be careful ! pVfifo_info may be NULL */
            result = cmd_receiver(pVfifo_info, &param_out, pCtrl_box);
            if( result < 0 )
            {
                err_msg("Stream CMD(0x%08x) error, result=%d\n", pCtrl_box->cmd, result);
            }
        }
    }
    return result;
}

