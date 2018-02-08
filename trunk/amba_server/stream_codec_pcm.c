
#include "util_def.h"
#include <stdlib.h>
#include <string.h>
#include "stream_fifo.h"
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

//=============================================================================
//                Global Data Definition
//=============================================================================

//=============================================================================
//                Private Function Definition
//=============================================================================
static int
_is_audio_frame(
    AMBA_FRAMEINFO_TYPE_e   frame_type)
{
    switch( frame_type )
    {
        case AMBA_FRAMEINFO_TYPE_AUDIO_FRAME:
        //case AMBA_FRAMEINFO_TYPE_EOS:
            return 1;
            break;
        default:
            return 0;
            break;
    }
    return 0;
}

static int
pcm_parse_frame_header(
    vfifo_info_t            *pVfifo_info,
    stream_codec_param_t    *pCodec_param,
    void                    *pExtra)
{
    int         result = 0;

    pVfifo_info->bExist_specific_info = true;
    return result;
}

static int
pcm_frame_pre_proc(
    vfifo_info_t            *pVfifo_info,
    stream_codec_param_t    *pCodec_param,
    void                    *pExtra)
{
    int         i = 0, result = 0;
    uint8_t     *pTarget_addr = 0;
    int         frame_payload_size = 0;

    if( _is_audio_frame(pCodec_param->frame_type) == 0 )
        return -101;

    pTarget_addr        = pCodec_param->pInput_start_addr;
    frame_payload_size  = pCodec_param->input_length;

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

    return result;
}
//=============================================================================
//                Public Function Definition
//=============================================================================
stream_codec_desc_t     stream_codec_pcm_desc =
{
    NULL,                   // struct stream_codec_desc    *next;
    "stream codec PCM",     // const char                  *name;
    AMP_FORMAT_MID_PCM,     // AMP_FORMAT_MID_e            codec_id;
    pcm_parse_frame_header, // int     (*parse_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    NULL,                   // int     (*destroy_specific_info)(stream_codec_param_t *pCodec_param, void *extra);
    pcm_frame_pre_proc,     // int     (*pre_proc)(stream_codec_param_t *pCodec_param, void *extra);
    NULL,                   // int     (*post_proc)(stream_codec_param_t *pCodec_param, void *extra);
};
