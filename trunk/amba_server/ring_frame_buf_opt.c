
// #include <stdlib.h>
#include "util_def.h"
#include "ring_frame_buf_opt.h"

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

//=============================================================================
//                Public Function Definition
//=============================================================================
int
rb_opt_init(
    rb_operator_t   *pRbOpt,
    unsigned char   *pStart_ptr,
    unsigned int    buf_size)
{
    if( !pRbOpt )   return -1;
    pRbOpt->pBuf_start_ptr                      = pStart_ptr + (buf_size & 0x3);
    pRbOpt->pRead_ptr[RB_READ_TYPE_FETCH]       = pRbOpt->pBuf_start_ptr;
    pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE]      = pRbOpt->pBuf_start_ptr;
    pRbOpt->pWrite_ptr                          = pRbOpt->pBuf_start_ptr;
    pRbOpt->pBuf_end_ptr                        = pStart_ptr + buf_size;
    pRbOpt->pValid_end_ptr[RB_READ_TYPE_FETCH]  = pRbOpt->pBuf_end_ptr;
    pRbOpt->pValid_end_ptr[RB_READ_TYPE_REMOVE] = pRbOpt->pBuf_end_ptr;
    return 0;
}

int
rb_opt_update_w(
    rb_operator_t       *pRbOpt,
    rb_w_data_info_t    *pData_info)
{
    int              i;
    unsigned char    *w_ptr = pRbOpt->pWrite_ptr;
    unsigned char    *r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE];
    unsigned int     item_size = 0;

    for(i = 0; i < pData_info->amount; i++)
    {
        item_size += pData_info->pData_size[i];
    }

    // 32 bits alignment
    item_size = (item_size + 0x3) & ~0x3;

    if( w_ptr > r_ptr )
    {
        if( (w_ptr + item_size) > pRbOpt->pBuf_end_ptr )
        {
            if( (pRbOpt->pBuf_start_ptr + item_size) >= r_ptr )
            {
                dbg_msg("\tNo space to write, drop current item !!\n");
                return -1;
            }

            pRbOpt->pValid_end_ptr[RB_READ_TYPE_FETCH]  = (unsigned char*)(((unsigned int)w_ptr + 0x3) & ~0x3);
            pRbOpt->pValid_end_ptr[RB_READ_TYPE_REMOVE] = (unsigned char*)(((unsigned int)w_ptr + 0x3) & ~0x3);

            w_ptr = pRbOpt->pBuf_start_ptr;
        }
    }
    else
    {
        if( w_ptr != r_ptr &&
            (w_ptr + item_size) >= r_ptr )
        {
            dbg_msg("\tW_pointer catch R_pointer, drop current item !!\n");
            return -1;
        }
    }

    for(i = 0; i < pData_info->amount; i++)
    {
        memcpy((void*)w_ptr, (void*)pData_info->ppData[i], pData_info->pData_size[i]);
        w_ptr += pData_info->pData_size[i];
    }

    // 32 bits alignment
    w_ptr = (unsigned char*)(((unsigned int)w_ptr + 0x3) & ~0x3);

    pRbOpt->pWrite_ptr = w_ptr;
    return 0;
}

int
rb_opt_update_r(
    rb_operator_t   *pRbOpt,
    rb_read_type_t  read_idx,
    unsigned char   **ppData,
    unsigned int    *pData_size,
    get_item_size   cb_get_item_size)
{
    unsigned char     *w_ptr;
    unsigned char     *r_ptr;
    unsigned int      item_size = 0;
    unsigned int      item_size_align = 0;
    unsigned int      bDummy_item = 0;

    if( cb_get_item_size == NULL )
        return  -1;

    switch( read_idx )
    {
        case RB_READ_TYPE_FETCH:
            w_ptr = pRbOpt->pWrite_ptr;
            r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_FETCH];
            break;
        case RB_READ_TYPE_REMOVE:
            w_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_FETCH];
            r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE];
            break;
        default:
            return -1;
    }

    cb_get_item_size(w_ptr, r_ptr, &item_size, &bDummy_item);
    if( item_size == 0 )
    {
        *ppData     = NULL;
        *pData_size = 0;
        return -2;
    }

    // 32 bits alignment
    item_size_align = (item_size + 0x3) & ~0x3;

    if( w_ptr < r_ptr ||
        (r_ptr + item_size_align) < w_ptr )
    {
        *ppData     = r_ptr;
        *pData_size = item_size;

        r_ptr += item_size_align;

        if( r_ptr == pRbOpt->pValid_end_ptr[read_idx] )
        {
            pRbOpt->pValid_end_ptr[read_idx] = pRbOpt->pBuf_end_ptr;
            r_ptr = pRbOpt->pBuf_start_ptr;
        }

        if( bDummy_item )
        {
            *pData_size = RB_INVALID_SIZE;
        }
    }
    else
    {
        *ppData     = NULL;
        *pData_size = 0;
    }

    pRbOpt->pRead_ptr[read_idx] = r_ptr;

    return 0;
}

int
rb_opt_peek_r(
    rb_operator_t   *pRbOpt,
    rb_read_type_t  read_idx,
    unsigned char   **ppData,
    unsigned int    *pData_size,
    get_item_size   cb_get_item_size)
{
    unsigned char     *w_ptr;
    unsigned char     *r_ptr;
    unsigned int      item_size = 0;
    unsigned int      item_size_align = 0;
    unsigned int      bDummy_item = 0;

    if( cb_get_item_size == NULL )
        return  -1;

    switch( read_idx )
    {
        case RB_READ_TYPE_FETCH:
            w_ptr = pRbOpt->pWrite_ptr;
            r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_FETCH];
            break;
        case RB_READ_TYPE_REMOVE:
            w_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_FETCH];
            r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE];
            break;
        default:
            return -1;
    }

    cb_get_item_size(w_ptr, r_ptr, &item_size, &bDummy_item);
    if( item_size == 0 )
    {
        *ppData     = NULL;
        *pData_size = 0;
        return -2;
    }

    // 32 bits alignment
    item_size_align = (item_size + 0x3) & ~0x3;

    if( w_ptr < r_ptr ||
        (r_ptr + item_size_align) < w_ptr )
    {
        *ppData     = r_ptr;
        *pData_size = item_size;
    }
    else
    {
        *ppData     = NULL;
        *pData_size = 0;
    }

    return 0;
}

int
rb_opt_confirm_space(
    rb_operator_t   *pRbOpt,
    unsigned int    target_size)
{
    unsigned char    *w_ptr = pRbOpt->pWrite_ptr;
    unsigned char    *r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE];
    unsigned int     remain_size = 0;

    if( w_ptr > r_ptr )
    {
        remain_size = pRbOpt->pBuf_end_ptr - w_ptr;
        remain_size += (r_ptr - pRbOpt->pBuf_start_ptr);
    }
    else if( w_ptr == r_ptr )
        remain_size = pRbOpt->pBuf_end_ptr - pRbOpt->pBuf_start_ptr;
    else
        remain_size = r_ptr - w_ptr;

    return (remain_size < target_size) ? 0 : 1;
}

int
rb_opt_remain_size(
    rb_operator_t   *pRbOpt)
{
    unsigned char    *w_ptr = pRbOpt->pWrite_ptr;
    unsigned char    *r_ptr = pRbOpt->pRead_ptr[RB_READ_TYPE_REMOVE];
    int              remain_size = 0;

    if( w_ptr > r_ptr )
    {
        remain_size = pRbOpt->pBuf_end_ptr - w_ptr;
        remain_size += (r_ptr - pRbOpt->pBuf_start_ptr);
    }
    else if( w_ptr == r_ptr )
        remain_size = pRbOpt->pBuf_end_ptr - pRbOpt->pBuf_start_ptr;
    else
        remain_size = r_ptr - w_ptr;

    return remain_size;
}

