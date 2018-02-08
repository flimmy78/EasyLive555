#ifndef __rb_opt_template_H_w2e8RcxR_lasx_HFnD_swSB_u3NtoMPaJ7aa__
#define __rb_opt_template_H_w2e8RcxR_lasx_HFnD_swSB_u3NtoMPaJ7aa__

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
//=============================================================================
//                Constant Definition
//=============================================================================
#define RB_INVALID_SIZE         0xFFFFFFFFu

typedef enum rb_read_type
{
    RB_READ_TYPE_FETCH       = 0,
    RB_READ_TYPE_REMOVE,
    RB_READ_TYPE_ALL,
} rb_read_type_t;
//=============================================================================
//                Macro Definition
//=============================================================================

//=============================================================================
//                Structure Definition
//=============================================================================
/**
 *  ring buffer operator, never stop when write data
 */
typedef struct rb_operator
{
    unsigned char     *pRead_ptr[RB_READ_TYPE_ALL];
    unsigned char     *pWrite_ptr;
    unsigned char     *pBuf_start_ptr;
    unsigned char     *pBuf_end_ptr;
    unsigned char     *pValid_end_ptr[RB_READ_TYPE_ALL];

} rb_operator_t;

typedef int (*get_item_size)(unsigned char *w_ptr, unsigned char *r_ptr, unsigned int *pItem_size, unsigned int *pIs_dummy_item);

/**
 *  ring buffer enqueue data info
 */
typedef struct rb_w_data_info
{
    unsigned int    amount;
    unsigned char   **ppData;       // pData[amount]
    unsigned int    *pData_size;    // data_size[amount]

} rb_w_data_info_t;
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
    unsigned int    buf_size);


int
rb_opt_update_w(
    rb_operator_t       *pRbOpt,
    rb_w_data_info_t    *pData_info);


int
rb_opt_update_r(
    rb_operator_t   *pRbOpt,
    rb_read_type_t  read_idx,
    unsigned char   **ppData,
    unsigned int    *pData_size,
    get_item_size   cb_get_item_size);


int
rb_opt_peek_r(
    rb_operator_t   *pRbOpt,
    rb_read_type_t  read_idx,
    unsigned char   **ppData,
    unsigned int    *pData_size,
    get_item_size   cb_get_item_size);


/**
 *  confirm request space
 *  return  0: fail, 1: ok
 */
int
rb_opt_confirm_space(
    rb_operator_t   *pRbOpt,
    unsigned int    target_size);

int
rb_opt_remain_size(
    rb_operator_t   *pRbOpt);


#ifdef __cplusplus
}
#endif

#endif
