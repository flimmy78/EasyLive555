#ifndef __util_def_H_wltTx9Yg_ljkr_HUV4_s1wM_umMiphhGWn41__
#define __util_def_H_wltTx9Yg_ljkr_HUV4_s1wM_umMiphhGWn41__

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
//                Constant Definition
//=============================================================================

//=============================================================================
//                Macro Definition
//=============================================================================
#define _toString(x)    #x
#define toStr(x)        _toString(x)

#if (1)
    #define err_msg(string, args...)        do{ fprintf(stderr, "[#%d]%s => ", __LINE__, __FUNCTION__);\
                                                fprintf(stderr, string, ## args); \
                                            }while(0)

    #define dbg_msg(string, args...)        fprintf(stderr, string, ## args);

    #define dbg_trace(string, args...)
    #define trace()

#elif defined(__STDC_VERSION__) // DEBUG
    #define err_msg(string, args...)        do{ fprintf(stderr, "[#%d]%s => ", __LINE__, __PRETTY_FUNCTION__);\
                                                fprintf(stderr, string, ## args); \
                                            }while(0)
    #define dbg_trace(string, args...)      //fprintf(stderr, "[#%d]%s\n", __LINE__, __PRETTY_FUNCTION__)

    #define dbg_msg(string, args...)        do{ fprintf(stderr, "[#%d]%s => ", __LINE__, __PRETTY_FUNCTION__);\
                                                fprintf(stderr, string, ## args); \
                                            }while(0)
    #define trace()                         fprintf(stderr, "[#%d]%s\n", __LINE__, __PRETTY_FUNCTION__)
#else /* #if defined(__STDC_VERSION__) */
    #if defined(__GNUC__)
    #define err_msg(string, args...)        do{ fprintf(stderr, "%s [#%d] => ", __FUNCTION__, __LINE__);\
                                                fprintf(stderr, string, ## args); \
                                            }while(0)
    #define dbg_trace(string, args...)      fprintf(stderr, "%s [#%d]\n", __FUNCTION__, __LINE__)

    #define dbg_msg(string, ...)            do{ fprintf(stderr, "%s [#%d] => ", __FUNCTION__, __LINE__);\
                                                fprintf(stderr, string, ## args); \
                                            }while(0)
    #define trace()                         fprintf(stderr, "%s [#%d]\n", __FUNCTION__, __LINE__)
    #else /** #if defined(__GNUC__) **/
    #define err_msg(string, ...)            do{ fprintf(stderr, "%s [#%d] => ", __FUNCTION__, __LINE__);\
                                                fprintf(stderr, string, __VA_ARGS__); \
                                            }while(0)
    #define dbg_trace(string, ...)          fprintf(stderr, "%s [#%d]\n", __FUNCTION__, __LINE__)

    #define dbg_msg                         do{ fprintf(stderr, "%s [#%d] => ", __FUNCTION__, __LINE__);\
                                                fprintf(stderr, string, __VA_ARGS__); \
                                            }while(0)
    #define trace()                         fprintf(stderr, "%s [#%d]\n", __FUNCTION__, __LINE__)
    #endif /** #if defined(__GNUC__) **/
#endif /* #if defined(__STDC_VERSION__) */



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

#ifdef __cplusplus
}
#endif

#endif
