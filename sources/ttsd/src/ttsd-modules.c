/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-modules.h>
#include <ttsd-config.h>

typedef struct {
    wstk_mutex_t                *mutex;
    wstk_hash_t                 *modules;       // name => module_entry_t
    wstk_hash_t                 *languages;     // id => mod_name
    ttsd_global_t               *ttsd_glb;
    bool                        fl_destroyed;
} ttsd_modules_manager_t;

typedef struct {
    wstk_dlo_t                  *dlo;
    ttsd_module_interface_t     *api;
    wstk_mutex_t                *mutex;
    uint32_t                    refs;
    char                        *name;          // refs to name
    char                        *path;          // refs to the list entry
    bool                        success_load;
    bool                        destroyed;
} module_entry_t;

static ttsd_modules_manager_t *manager = NULL;
static module_entry_t *module_lookup(const char *name, bool lock);

// --------------------------------------------------------------------------------------------------------------
static void destructor__ttsd_modules_manager_t(void *data) {
    ttsd_modules_manager_t *obj = (ttsd_modules_manager_t *)data;

    if(!obj || obj->fl_destroyed) { return; }
    obj->fl_destroyed = true;

    if(obj->languages) {
        wstk_mutex_lock(obj->mutex);
        wstk_mem_deref(obj->languages);
        wstk_mutex_unlock(obj->mutex);
    }
    if(obj->modules) {
        wstk_mutex_lock(obj->mutex);
        wstk_mem_deref(obj->modules);
        wstk_mutex_unlock(obj->mutex);
    }

    wstk_mem_deref(obj->mutex);
}

static void destructor__module_entry_t(void *data) {
    module_entry_t *obj = (module_entry_t *)data;
    bool floop = true;

    if(!obj || obj->destroyed) {
        return;
    }

#ifdef TTSD_DEBUG
    log_debug("Unloading module: (%s) [refs=%u]", obj->name, obj->refs);
#endif

    if(obj->mutex) {
        while(floop) {
            wstk_mutex_lock(obj->mutex);
            floop = (obj->refs > 0);
            wstk_mutex_unlock(obj->mutex);

            WSTK_SCHED_YIELD(0);
        }
    }

    if(obj->success_load) {
        if(obj->api) {
            obj->api->unload();
        }
    }

    if(obj->dlo) {
        wstk_dlo_close(&obj->dlo);
    }

    wstk_mem_deref(obj->mutex);
}

static void modlist_foreach_cb_onload(uint32_t idx, void *data, void *udata) {
    ttsd_config_entry_module_t *descr = (ttsd_config_entry_module_t *)data;
    module_entry_t *entry = NULL;
    ttsd_module_interface_t *api = NULL;
    wstk_dlo_t *dlo = NULL;
    wstk_status_t st = WSTK_STATUS_FALSE;
    int err = 0;

    st = wstk_dlo_open(&dlo, descr->path);
    if(st != WSTK_STATUS_SUCCESS) {
        goto out;
    }

    st = wstk_dlo_sym(dlo, "ttsd_module_interface_v1", (void *)&api);
    if(st != WSTK_STATUS_SUCCESS) { goto out; }

    if(wstk_str_is_empty(api->name)) {
        log_warn("Module name is empty!");
        st = WSTK_STATUS_INVALID_PARAM;
        goto out;
    }

    if(module_lookup(api->name, false)) {
        log_warn("Module already loaded (%s)", api->name);
        st = WSTK_STATUS_ALREADY_EXISTS;
        goto out;
    }

    st = wstk_mem_zalloc((void *)&entry, sizeof(module_entry_t), destructor__module_entry_t);
    if(st != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mem_zalloc()");
        goto out;
    }
    if((st = wstk_mutex_create(&entry->mutex)) != WSTK_STATUS_SUCCESS) {
        log_error("wstk_mutex_create()");
        goto out;
    }

    entry->dlo = dlo;
    entry->api = api;
    entry->name = (char *)api->name;
    entry->path = descr->path;

    if((err = api->load()) == 0) {
        entry->success_load = true;

        wstk_mutex_lock(manager->mutex);
        st = wstk_hash_insert_ex(manager->modules, api->name, entry, true);
        wstk_mutex_unlock(manager->mutex);
    } else {
        log_warn("Unable to load module (err=%i)", err);
        st = WSTK_STATUS_FALSE;
    }

    if(st == WSTK_STATUS_SUCCESS) {
        char *langs[32] = {0};
        uint32_t lcnt = 0;
        char *tmp = wstk_str_dup(descr->languages);
        if(tmp) {
            lcnt = wstk_str_separate(tmp, ',', langs, ARRAY_SIZE(langs));
            if(lcnt > 0) {
                for(int i=0; i < lcnt; i++) {
                    if(!langs[i]) continue;
                    char *n = ttsd_trim(langs[i]);

                    wstk_mutex_lock(manager->mutex);
                    module_entry_t *oe = wstk_hash_find(manager->languages, n);
                    if(oe) {
                        log_warn("Language already registered [%s] => (%s)", n, oe->name);
                    } else {
                        if(wstk_hash_insert_ex(manager->languages, n, api->name, false) == WSTK_STATUS_SUCCESS) {
                            log_notice("Language registered (%s => %s)", n, api->name);
                        } else {
                            log_warn("Unable to register language (%s => %s)", n, api->name);
                        }
                    }
                    wstk_mutex_unlock(manager->mutex);
                }
            }
        }
        wstk_mem_deref(tmp);
    }

out:
    if(st != WSTK_STATUS_SUCCESS) {
        if(entry) {
            wstk_mem_deref(entry);
        } else {
            if(dlo) { wstk_dlo_close(&dlo); }
        }
        log_warn("Unable to load module (%s)", descr->path);
    } else {
        log_notice("Module loaded (%s)", api->name);
    }
}

static module_entry_t *module_lookup(const char *name, bool lock) {
    module_entry_t *entry = NULL;

    if(!manager || !name) {
        return NULL;
    }

    wstk_mutex_lock(manager->mutex);
    entry = wstk_hash_find(manager->modules, name);
    if(entry && lock) {
        wstk_mutex_lock(entry->mutex);
        entry->refs++;
        wstk_mutex_unlock(entry->mutex);
    }
    wstk_mutex_unlock(manager->mutex);

    return entry;
}

static void module_release(module_entry_t *entry) {
    if(entry) {
        wstk_mutex_lock(entry->mutex);
        if(entry->refs) entry->refs--;
        wstk_mutex_unlock(entry->mutex);
    }
}

static char *language_lookup(const char *name) {
    char *mod_name = NULL;

    if(!manager || !name) {
        return NULL;
    }

    wstk_mutex_lock(manager->mutex);
    mod_name = wstk_hash_find(manager->languages, name);
    wstk_mutex_unlock(manager->mutex);

    return mod_name;
}


// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// public
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
wstk_status_t ttsd_modules_manager_init(ttsd_global_t *global) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;

    status = wstk_mem_zalloc((void *)&manager, sizeof(ttsd_modules_manager_t), destructor__ttsd_modules_manager_t);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    if((status = wstk_mutex_create(&manager->mutex)) != WSTK_STATUS_SUCCESS) {
        goto out;
    }

    if((status = wstk_hash_init(&manager->modules)) != WSTK_STATUS_SUCCESS) {
        goto out;
    }
    if((status = wstk_hash_init(&manager->languages)) != WSTK_STATUS_SUCCESS) {
        goto out;
    }

    manager->ttsd_glb = global;

#ifdef TTSD_DEBUG
    log_debug("Loading modules");
#endif

    wstk_list_foreach(global->modules, modlist_foreach_cb_onload, NULL);

out:
    return status;
}

wstk_status_t ttsd_modules_manager_shutdown() {
    wstk_status_t status = WSTK_STATUS_SUCCESS;

    if(!manager || manager->fl_destroyed) {
        return WSTK_STATUS_SUCCESS;
    }

    wstk_mem_deref(manager);

    return status;
}

bool ttsd_module_exists(const char *name) {
    module_entry_t *me = NULL;

    if(!manager || manager->fl_destroyed) {
        return false;
    }

    me = module_lookup(name, false);
    return (me ? true : false);
}

bool ttsd_language_exists(const char *name) {
    char *mn = NULL;

    if(!manager || manager->fl_destroyed) {
        return false;
    }

    mn = language_lookup(name);
    return (mn ? true : false);
}

wstk_status_t ttsd_synthesis(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    ttsd_synthesis_result_t *rlocal = NULL;
    module_entry_t *me = NULL;
    char *mod_name = NULL;

    if(!manager || manager->fl_destroyed) {
        return WSTK_STATUS_DESTROYED;
    }

    if(!result || !params) {
        return WSTK_STATUS_INVALID_PARAM;
    }

    if(!(mod_name = language_lookup(params->language))) {
        return WSTK_STATUS_NOT_FOUND;
    }

    if(!(me = module_lookup(mod_name, true))) {
        return WSTK_STATUS_NOT_FOUND;
    }

    if((status = me->api->synthesize(&rlocal, params)) == WSTK_STATUS_SUCCESS) {
        *result = rlocal;
    } else {
        wstk_mem_deref(rlocal);
    }

    module_release(me);

    return status;
}
