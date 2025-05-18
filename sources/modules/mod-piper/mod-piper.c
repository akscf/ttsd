/**
 **
 ** (C)2025 aks
 **/
#include <ttsd.h>
#include <mod-piper.h>
#include <piper_api.h>
#include <piper_api.h>
#include <espeak-ng/speak_lib.h>

mod_piper_global_t *globals;

static wstk_status_t mod_load() {
    wstk_status_t status = WSTK_STATUS_SUCCESS;

    if((status = mod_piper_global_init(&globals)) != WSTK_STATUS_SUCCESS) {
        goto out;
    }

    if((status = mod_piper_load_config()) != WSTK_STATUS_SUCCESS) {
        goto out;
    }

    if(status == WSTK_STATUS_SUCCESS) {
        log_notice("Module version (%s)", MOD_VERSION);
    }

    if(globals->espeak_data_path) {
        int result = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, globals->espeak_data_path, 0);
        if (result < 0) {
            log_error("Unable to initialize eSpeak-ng");
            status = WSTK_STATUS_FALSE; goto out;
        }
    }

    if(!wstk_hash_is_empty(globals->models)) {
        wstk_hash_index_t *hidx = NULL;
        mod_piper_model_descr_t *entry = NULL;

        for(hidx = wstk_hash_first_iter(globals->models, hidx); hidx; hidx = wstk_hash_next(&hidx)) {
            wstk_hash_this(hidx, NULL, NULL, (void *)&entry);
            if(entry && entry->preload) {
                if((status = piper_init(entry)) != WSTK_STATUS_SUCCESS) {
                    log_error("Unable to prelaod model (%s)", entry->model);
                    break;
                }
            }
        }
    }

out:
    return status;
}

static void mod_unload() {
    if(globals)  {
        if(globals->fl_espeak_init_ok) {
            espeak_Terminate();
        }

        wstk_mem_deref(globals);
    }
}

static wstk_status_t synthesize(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params) {
    return piper_synthesize(result, params);
}

TTSD_MODULE_DEFINITION(mod_piper, mod_load, mod_unload, synthesize);

