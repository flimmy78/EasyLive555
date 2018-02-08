
#include "util_def.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stream_codec.h"
#include "stream_command.h"
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
_is_video_frame(
    AMBA_FRAMEINFO_TYPE_e   frame_type)
{
    switch( frame_type )
    {
        case AMBA_FRAMEINFO_TYPE_IDR_FRAME:
        case AMBA_FRAMEINFO_TYPE_I_FRAME:
        case AMBA_FRAMEINFO_TYPE_P_FRAME:
        case AMBA_FRAMEINFO_TYPE_B_FRAME:
        //case AMBA_FRAMEINFO_TYPE_EOS:
            return 1;
            break;
        default:
            return 0;
            break;
    }
    return 0;
}

#if 0
static void
_dump_sps_pps(
    stream_codec_h264_info_t    *pH264_info)
{
    int     i;

    if( pH264_info == NULL)
        return;

    fprintf(stderr, "\n\t=> SPS (%u): \n\t", pH264_info->sps_len);
    for(i = 0; i < pH264_info->sps_len; i++)
    {
        fprintf(stderr, "%02x ", pH264_info->sps[i]);
    }

    fprintf(stderr, "\n\t=> PPS (%u): \n\t", pH264_info->pps_len);
    for(i = 0; i < pH264_info->pps_len; i++)
    {
        fprintf(stderr, "%02x ", pH264_info->pps[i]);
    }
    fprintf(stderr, "\n\n");
    return;
}
#else
#define _dump_sps_pps(a)
#endif

static int
h264_parse_sps_pps(
    vfifo_info_t            *pVfifo_info,
    stream_codec_param_t    *pCodec_param,
    void                    *pExtra)
{
    int         result = 0;
    uint8_t     *pCur = pCodec_param->pInput_start_addr;
    uint32_t    remain_size = pCodec_param->input_length;
    uint32_t    start_code = 0;
    uint32_t    pos;
    int         sps_end, len, pad;
    uint32_t    bFound = false;

    stream_codec_h264_info_t    *pH264_info = 0;

    // SPS/PPS exist at IDR frame
    if( pCodec_param->frame_type != AMBA_FRAMEINFO_TYPE_IDR_FRAME )
    {
        return -100;
    }

    dbg_trace();

    //----------------------
    // check specific info exist or not
    if( pVfifo_info->specific_info.pH264_info  )
    {
        pH264_info = pVfifo_info->specific_info.pH264_info;
    }
    else
    {
        pH264_info = malloc(sizeof(stream_codec_h264_info_t));
        if( !pH264_info )
        {
            err_msg("mallco h264 specific info fail !!\n");
            result = -1;
            return result;
        }
        pVfifo_info->specific_info.pH264_info = pH264_info;
    }
    memset(pH264_info, 0x0, sizeof(stream_codec_h264_info_t));

    // playback mode should use special behavior
    if( pVfifo_info->stream_type == STREAM_TYPE_PLAYBACK )
    {
        stream_reader_ctrl_box_t    ctrl_box = {0};

        ctrl_box.cmd                        = STREAM_READER_CMD_PLAYBACK_GET_SPS_PPS;
        ctrl_box.out.get_sps_pps.pH264_info = pH264_info;
        result = stream_cmd_control(pVfifo_info, &ctrl_box);
        if( result == 0 )
        {
            pVfifo_info->bExist_specific_info = true;
            return result;
        }

        memset(pH264_info, 0x0, sizeof(stream_codec_h264_info_t));
    }

    if( pCur == NULL || remain_size == 0 )
    {
        result = -101;
        return result;
    }

    //----------------------
    // verify nal type
    if( (pCur[4] & 0x1F) == 0x09 )
    {
        // Skip AU Delimiter at the begining
        pCur += 6;
        remain_size -= 6;
    }

    if( (pCur[4] & 0x1F) != 0x07 )
    {
        // not SPS
        err_msg("Can NOT find SPS !\n");
        result = -1;
        return result;
    }

    //----------------------
    // find end of SPS
    start_code = 0;
    for(pos = 4, bFound = 0; (bFound == false) && (pos < remain_size); pos++)
    {
        if( start_code == 0x00000001 )
        {
            bFound = true;
            break;
        }
        else
        {
            start_code = (start_code << 8) | pCur[pos];
        }
    }

    if( bFound == false )
    {
        err_msg("Can NOT find sps !!\n");
        result = -2;
        return result;
    }

    sps_end = pos - 5;
    len     = sps_end - 4 + 1;
    pad     = len & 3;
    if( pad > 0 )
    {
        pad = 4 - pad;
    }

    pH264_info->sps_len = len + pad;
    if( pH264_info->sps_len > 64 )
    {
        pH264_info->sps_len = 0;
        err_msg("Out sps buffer size !!\n");
        result = -3;
        return result;
    }

    memcpy(pH264_info->sps, &pCur[4], len);  // pCur[4] skip start code

    pH264_info->profile_level = (pH264_info->sps[5] << 16) |
                                (pH264_info->sps[6] << 8) |
                                (pH264_info->sps[7]);

    //----------------------
    // find End of PPS, previous stop should be pps_start
    if( (pCur[pos] & 0x1F) != 0x08 )
    {
        // not PPS
        err_msg("Can NOT find PPS(%02x) !!\n", pCur[pos]);
        result = -4;
        return result;
    }

    start_code = 0;
    for(bFound = false; (bFound == false) && (pos < remain_size); pos++)
    {
        if( start_code == 0x00000001 )
        {
            bFound = true;
            break;
        }
        else
        {
            start_code = (start_code << 8) | pCur[pos];
        }
    }

    if( bFound == false )
    {
        err_msg("Can NOT find pps_end\n");
        result = -5;
        return result;
    }

    len = pos - sps_end - 9; //(pos - 5) - (sps_end + 5) + 1;
    pad = len & 0x3;
    if( pad > 0 )
    {
        pad = 4 - pad;
    }

    pH264_info->pps_len = len + pad;
    if( pH264_info->pps_len > 32 )
    {
        pH264_info->pps_len = 0;
        err_msg("Out pps buffer size !!\n");
        result = -6;
        return result;
    }

    memcpy(pH264_info->pps, &pCur[sps_end + 5], len); // pCur[sps_end + 1 + 4]: skip start code

    pVfifo_info->bExist_specific_info = true;

    _dump_sps_pps(pH264_info);

    dbg_trace();
    return result;
}

static int
h264_destroy_sps_pps(
    vfifo_info_t            *pVfifo_info,
    stream_codec_param_t    *pCodec_param,
    void                    *pExtra)
{
    int         result = 0;
    dbg_trace();

    // TODO: need mutex ???
    if( pVfifo_info->specific_info.pH264_info )
    {
        free(pVfifo_info->specific_info.pH264_info);
        pVfifo_info->specific_info.pH264_info = NULL;
    }

    dbg_trace();
    return result;
}

static int
h264_frame_pre_proc(
    vfifo_info_t            *pVfifo_info,
    stream_codec_param_t    *pCodec_param,
    void                    *pExtra)
{
    int                 i = 0, result = 0;
    uint8_t             nal_unit_type = 0xFF;
    uint8_t             *pNal_unit_start = 0, *pTarget_addr = 0;
    int                 frame_payload_size = 0, remain = 0;

    dbg_trace();

    if( _is_video_frame(pCodec_param->frame_type) == 0 )
        return -101;

    /*  RTOS fifo is ring buffer
     *                       |-->size-->|
     *  case 1.   |----------r----------w-----|
     *
     *            --->|              |--size-->
     *  case 2.   |---w--------------r--------|
     */
    remain = pCodec_param->pInput_limit_addr - pCodec_param->pInput_start_addr;
    pNal_unit_start = (remain < 4)
                    ? pCodec_param->pInput_base_addr + (4 - remain)
                    : pCodec_param->pInput_start_addr + 4;

    nal_unit_type       = pNal_unit_start[0] & 0x1f;
    frame_payload_size  = pCodec_param->input_length;

    if( nal_unit_type == 0x09 )
    {
        // skip H264 Access Unit delimiter (6 bytes)
        pTarget_addr = (remain < 6)
                     ? pCodec_param->pInput_base_addr + (6 - remain)
                     : pCodec_param->pInput_start_addr + 6;

        frame_payload_size -= 6;
    }
    else
    {
        // no au-delimiter
        pTarget_addr = pCodec_param->pInput_start_addr;
    }

    // reset output info
    pCodec_param->bWrap                    = false;
    pCodec_param->pValidated_start_addr[0] = NULL;
    pCodec_param->validated_length[0]      = 0;
    pCodec_param->pValidated_start_addr[1] = NULL;
    pCodec_param->validated_length[1]      = 0;

    // assign target addr/size
    i = 0;
    if( (pTarget_addr + frame_payload_size) > pCodec_param->pInput_limit_addr )
    {
        pCodec_param->pValidated_start_addr[i] = pTarget_addr;
        pCodec_param->validated_length[i]      = pCodec_param->pInput_limit_addr - pTarget_addr;

        // ring back to head
        pCodec_param->bWrap = true;
        pTarget_addr = pCodec_param->pInput_base_addr;
        frame_payload_size -= pCodec_param->validated_length[i];

        i++;
    }

    pCodec_param->pValidated_start_addr[i] = pTarget_addr;
    pCodec_param->validated_length[i]      = frame_payload_size;

    dbg_trace();
    return result;
}
//=============================================================================
//                Public Function Definition
//=============================================================================
stream_codec_desc_t     stream_codec_h264_desc =
{
    NULL,                  // struct stream_codec_desc    *next;
    "stream codec h264",   // const char                  *name;
    AMP_FORMAT_MID_H264,   // AMP_FORMAT_MID_e            codec_id;
    h264_parse_sps_pps,    // int     (*parse_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    h264_destroy_sps_pps,  // int     (*destroy_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    h264_frame_pre_proc,   // int     (*pre_proc)(stream_codec_param_t *pCodec_param, void *extra);
    NULL,                  // int     (*post_proc)(stream_codec_param_t *pCodec_param, void *extra);
};

stream_codec_desc_t     stream_codec_avc_desc =
{
    NULL,                  // struct stream_codec_desc    *next;
    "stream codec avc",   // const char                  *name;
    AMP_FORMAT_MID_AVC,    // AMP_FORMAT_MID_e            codec_id;
    h264_parse_sps_pps,    // int     (*parse_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    h264_destroy_sps_pps,  // int     (*destroy_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    h264_frame_pre_proc,   // int     (*pre_proc)(stream_codec_param_t *pCodec_param, void *extra);
    NULL,                  // int     (*post_proc)(stream_codec_param_t *pCodec_param, void *extra);
};
