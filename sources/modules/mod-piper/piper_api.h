/**
 **
 ** (C)2025 aks
 **/
#ifndef MOD_PIPER_API_H
#define MOD_PIPER_API_H
#include <mod-piper.h>

#ifdef __cplusplus
extern "C" {
#endif

wstk_status_t piper_init(mod_piper_model_descr_t *model);
wstk_status_t piper_destroy(mod_piper_model_descr_t *model);
wstk_status_t piper_synthesize(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params);

#ifdef __cplusplus
}
#endif
#endif
