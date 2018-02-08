
#ifndef __stream_codec_H_w7uOT7eq_lUdV_HLKr_shR6_uEkIdONDjJKZ__
#define __stream_codec_H_w7uOT7eq_lUdV_HLKr_shR6_uEkIdONDjJKZ__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"
#include "AmbaFrameInfo.h"
//=============================================================================
//                Constant Definition
//=============================================================================
#ifndef true
    #define true    1
#endif

#ifndef false
    #define false    0
#endif

/**
 *  Codec Id defined by AMBA, FormatDef.h
 *  We map them into ffmpeg definitions
 */
#define AMP_FORMAT_MID_A_MASK   0x20
#define AMP_FORMAT_MID_T_MASK   0x40

typedef enum _AMP_FORMAT_MID_e_
{
    AMP_FORMAT_MID_H264 = 0x01,                             /**< H264 media id */
    AMP_FORMAT_MID_AVC = 0x02,                              /**< AVC media id */
    AMP_FORMAT_MID_MJPEG = 0x03,                            /**< MJPEG media id */
    AMP_FORMAT_MID_AAC = (AMP_FORMAT_MID_A_MASK | 0x01),    /**< AAC media id */
    AMP_FORMAT_MID_PCM = (AMP_FORMAT_MID_A_MASK | 0x02),    /**< PCM media id */
    AMP_FORMAT_MID_ADPCM = (AMP_FORMAT_MID_A_MASK | 0x03),  /**< ADPCM media id */
    AMP_FORMAT_MID_MP3 = (AMP_FORMAT_MID_A_MASK | 0x04),    /**< MP3 media id */
    AMP_FORMAT_MID_AC3 = (AMP_FORMAT_MID_A_MASK | 0x05),    /**< AC3 media id */
    AMP_FORMAT_MID_WMA = (AMP_FORMAT_MID_A_MASK | 0x06),    /**< WMA media id */
    AMP_FORMAT_MID_OPUS = (AMP_FORMAT_MID_A_MASK | 0x07),   /**< OPUS media id */
    AMP_FORMAT_MID_TEXT = (AMP_FORMAT_MID_T_MASK | 0x01),   /**< Text media id */
    AMP_FORMAT_MID_MP4S = (AMP_FORMAT_MID_T_MASK | 0x02),   /**< MP4S media id */
    AMP_FORMAT_MID_UNKNOW  = 0xFFFFFFFF,
} AMP_FORMAT_MID_e;
//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  specific info for H264
 */
typedef struct stream_codec_h264_info
{
    uint8_t     sps[64];   /**< H264 SPS */
    uint32_t    sps_len;   /**< H264 SPS size */
    uint8_t     pps[32];   /**< H264 PPS */
    uint32_t    pps_len;   /**< H264 PPS size */
    uint32_t    profile_level;

} stream_codec_h264_info_t;

/**
 *  stream codec parameters
 */
typedef struct stream_codec_param
{
    AMBA_FRAMEINFO_TYPE_e   frame_type;
    uint32_t                frame_num;
    uint8_t                 *pInput_base_addr;
    uint8_t                 *pInput_limit_addr;

    uint8_t                 *pInput_start_addr;
    uint32_t                input_length;

    // return valid frame addr/size if do pre_proc_frame() or post_proc_frame()
    uint32_t                bWrap;
    uint8_t                 *pValidated_start_addr[2];
    uint32_t                validated_length[2];

    void                    *pPriv_info;

} stream_codec_param_t;

/**
 *  stream frame codec descriptor
 */
struct vfifo_info;
typedef struct stream_codec_desc
{
    struct stream_codec_desc    *next;
    const char                  *name;

    AMP_FORMAT_MID_e            codec_id;

    // pFifo_info should refer struct vfifo_info in stream_fifo.h
    int     (*parse_specific_info)(struct vfifo_info *pFifo_info, stream_codec_param_t *pCodec_param, void *pExtra);
    int     (*destroy_specific_info)(struct vfifo_info *pFifo_info, stream_codec_param_t *pCodec_param, void *pExtra);
    int     (*pre_proc_frame)(struct vfifo_info *pFifo_info, stream_codec_param_t *pCodec_param, void *pExtra);
    int     (*post_proc_frame)(struct vfifo_info *pFifo_info, stream_codec_param_t *pCodec_param, void *pExtra);

} stream_codec_desc_t;
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
