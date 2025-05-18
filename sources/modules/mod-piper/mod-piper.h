/**
 **
 ** (C)2025 aks
 **/
#ifndef MOD_PIPER_H
#define MOD_PIPER_H
#include <ttsd.h>

#define MOD_VERSION "1.0.1"
//#define MOD_PIPER_DEBUG

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    wstk_mutex_t    *mutex;
    wstk_mutex_t    *mutex_espeak;
    wstk_hash_t     *models;
    char            *espeak_data_path;
    uint32_t        active_threads;
    bool            fl_use_gpu;
    bool            fl_shutdown;
    bool            fl_espeak_init_ok;
} mod_piper_global_t;

typedef struct {
    char            *lang;
    char            *model;
    void            *piper;
    bool            preload;
    bool            fl_destroy;
} mod_piper_model_descr_t;

/* misc.c */
wstk_status_t mod_piper_global_init(mod_piper_global_t **out);
wstk_status_t mod_piper_load_config();

mod_piper_model_descr_t *mod_piper_lookup_model(char *lang);

#ifdef __cplusplus
}
#endif
#endif
