/**
 **
 ** (C)2025 aks
 **/
#include <mod-piper.h>
#include <piper_api.h>

extern mod_piper_global_t *globals;

static void destructor__mod_piper_global_t(void *data) {
    mod_piper_global_t *obj = (mod_piper_global_t *)data;
    bool floop = true;

    if(!obj || obj->fl_shutdown) { return; }
    obj->fl_shutdown = true;

    obj->espeak_data_path = wstk_mem_deref(obj->espeak_data_path);

    if(obj->models) {
        if(globals->mutex) wstk_mutex_lock(globals->mutex);
        obj->models = wstk_mem_deref(obj->models);
        if(globals->mutex) wstk_mutex_unlock(globals->mutex);
    }

    obj->mutex = wstk_mem_deref(obj->mutex);
    obj->mutex_espeak = wstk_mem_deref(obj->mutex_espeak);

}

static void destructor__mod_piper_model_descr_t(void *data) {
    mod_piper_model_descr_t *obj = (mod_piper_model_descr_t *)data;
    bool floop = true;

    if(!obj || obj->fl_destroy) return;
    obj->fl_destroy = true;

    if(obj->piper) {
        piper_destroy(obj);
    }

    wstk_mem_deref(obj->lang);
    wstk_mem_deref(obj->model);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// public
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
wstk_status_t mod_piper_model_descr_alloc(mod_piper_model_descr_t **out, char *lang, char *model, bool preload) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    mod_piper_model_descr_t *descr = NULL;

    status = wstk_mem_zalloc((void *)&descr, sizeof(mod_piper_model_descr_t), destructor__mod_piper_model_descr_t);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mem_zalloc()");
        goto out;
    }

    descr->lang = lang;
    descr->model = model;
    descr->preload = preload;

    *out = descr;
out:
    return status;
}

wstk_status_t mod_piper_global_init(mod_piper_global_t **out) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    mod_piper_global_t *glb = NULL;

    status = wstk_mem_zalloc((void *)&glb, sizeof(mod_piper_global_t), destructor__mod_piper_global_t);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mem_zalloc()");
        goto out;
    }

    if((status = wstk_mutex_create(&glb->mutex)) != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mutex_create()");
        goto out;
    }
    if((status = wstk_mutex_create(&glb->mutex_espeak)) != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mutex_create()");
        goto out;
    }
    if((status = wstk_hash_init(&glb->models)) != WSTK_STATUS_SUCCESS) {
        log_error("wstk_hash_init()");
        goto out;
    }

out:
    if(status == WSTK_STATUS_SUCCESS) {
        *out = glb;
    } else {
        wstk_mem_deref(glb);
    }
    return status;
}

wstk_status_t mod_piper_load_config() {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    char *config_path = NULL;
    ezxml_t xml, xelem, xparams;

    wstk_sdprintf(&config_path, "%s/mod-piper-conf.xml", ttsd_core_get_path_config());

    if(!wstk_file_exists(config_path)) {
        log_error("File not found: %s", config_path);
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    if((xml = ezxml_parse_file(config_path)) == NULL) {
        log_error("Unable to parse configuration (%s)", config_path);
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    if(!wstk_str_equal(ezxml_name(xml), "configuration", false)) {
        log_error("Missing root element: <configuration>");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    if((xparams = ezxml_child(xml, "settings")) != NULL) {
        for(ezxml_t xparam = ezxml_child(xparams, "param"); xparam; xparam = xparam->next) {
            const char *name = ezxml_attr(xparam, "name");
            const char *val = ezxml_attr(xparam, "value");

            if(wstk_str_is_empty(name)) { continue; }

            if(wstk_str_equal(name, "use-gpu", false)) {
                globals->fl_use_gpu = wstk_str_atob(val);
            }
            else if(wstk_str_equal(name, "espeak-data", false)) {
                if(val) {
                    if(!wstk_dir_exists(val)) {
                        log_error("Path not found (%s)", val);
                        status = WSTK_STATUS_FALSE; goto out;
                    }
                    wstk_str_dup2(&globals->espeak_data_path, val);
                }
            }
        }
    }

    if((xelem = ezxml_child(xml, "languages")) != NULL) {
        for(ezxml_t xparam = ezxml_child(xelem, "language"); xparam; xparam = xparam->next) {
            const char *id = ezxml_attr(xparam, "id");
            const char *model = ezxml_attr(xparam, "model");
            const char *preloadstr = ezxml_attr(xparam, "preload");
            mod_piper_model_descr_t *descr = NULL;

            if(wstk_str_is_empty(id) || wstk_str_is_empty(model)) {
                continue;
            }

            if(!wstk_file_exists(model)) {
                log_error("Model not found (%s)", model);
                continue;
            }

            status = mod_piper_model_descr_alloc(&descr, wstk_str_dup(id), wstk_str_dup(model), wstk_str_atob(preloadstr));
            if(status != WSTK_STATUS_SUCCESS) {
                log_error("mod_piper_model_descr_alloc()");
                goto out;
            }

            wstk_mutex_lock(globals->mutex);
            wstk_hash_insert_ex(globals->models, id, descr, true);
            wstk_mutex_unlock(globals->mutex);

            log_notice("Language registered (%s)", id);
        }
    }

out:
    wstk_mem_deref(config_path);
    if(xml) { ezxml_free(xml); }
    return status;
}

