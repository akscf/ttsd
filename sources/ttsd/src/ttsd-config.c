/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-config.h>

static void destructor__ttsd_global_t(void *data) {
    ttsd_global_t *obj = (ttsd_global_t *)data;

    if(!obj) { return; }

    wstk_mem_deref(obj->file_config);
    wstk_mem_deref(obj->file_pid);

    wstk_mem_deref(obj->path_home);
    wstk_mem_deref(obj->path_config);
    wstk_mem_deref(obj->path_modules);
    wstk_mem_deref(obj->path_models);
    wstk_mem_deref(obj->path_var);
    wstk_mem_deref(obj->path_tmp);

    if(obj->http_server) {
        wstk_mem_deref(obj->http_server->secret);
        wstk_mem_deref(obj->http_server->address);
        wstk_mem_deref(obj->http_server);
    }

    wstk_mem_deref(obj->modules);
    wstk_mem_deref(obj->mutex);
}

static void destructor__ttsd_config_entry_module_t(void *data) {
    ttsd_config_entry_module_t *obj = (ttsd_config_entry_module_t *)data;

    if(!obj) { return; }

    wstk_mem_deref(obj->path);
    wstk_mem_deref(obj->languages);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// public
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
wstk_status_t ttsd_global_init(ttsd_global_t **global) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    ttsd_global_t *ttsd_local = NULL;

    status = wstk_mem_zalloc((void *)&ttsd_local, sizeof(ttsd_global_t), destructor__ttsd_global_t);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    status = wstk_mutex_create(&ttsd_local->mutex);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    /* http-server */
    status = wstk_mem_zalloc((void *)&ttsd_local->http_server, sizeof(ttsd_config_entry_http_server_t), NULL);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    /* modules */
    status = wstk_list_create(&ttsd_local->modules);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    *global = ttsd_local;
out:
    if(status != WSTK_STATUS_SUCCESS) {
        wstk_mem_deref(ttsd_local);
    }
    return status;
}

wstk_status_t ttsd_config_load(ttsd_global_t *global) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    const char *conf_version_str = NULL;
    ezxml_t xml, xelem, xparams;

    if(!wstk_file_exists(global->file_config)) {
        log_error("File not found: %s", global->file_config);
        return WSTK_STATUS_FALSE;
    }

    if((xml = ezxml_parse_file(global->file_config)) == NULL) {
        log_error("Unable to parse configuration (%s)", global->file_config);
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    if(!wstk_str_equal(ezxml_name(xml), "configuration", false)) {
        log_error("Missing root element: <configuration>");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    conf_version_str = ezxml_attr(xml, "version");
    if(conf_version_str) {
        int vi = atoi(conf_version_str);
        if(vi < CONFIG_VERSION) {
            log_error("Configuration is outdated (%d) (expected version >= %i)", vi, CONFIG_VERSION);
            wstk_goto_status(WSTK_STATUS_FALSE, out);
        }
    }

    /* http-server */
    if((xelem = ezxml_child(xml, "http-server")) != NULL) {
        if((xparams = ezxml_child(xelem, "settings")) != NULL) {
            for(ezxml_t xparam = ezxml_child(xparams, "param"); xparam; xparam = xparam->next) {
                const char *name = ezxml_attr(xparam, "name");
                const char *val = ezxml_attr(xparam, "value");

                if(wstk_str_is_empty(name)) { continue; }

                if(wstk_str_equal(name, "address", false)) {
                    wstk_str_dup2(&global->http_server->address, val);
                }
                else if(wstk_str_equal(name, "secret", false)) {
                    wstk_str_dup2(&global->http_server->secret, val);
                }
                else if(wstk_str_equal(name, "port", false)) {
                    global->http_server->port = wstk_str_atoi(val);
                }
                else if(wstk_str_equal(name, "max-conns", false)) {
                    global->http_server->max_conns = wstk_str_atoi(val);
                }
                else if(wstk_str_equal(name, "max-idle", false)) {
                    global->http_server->max_idle = wstk_str_atoi(val);
                }
            }
        }
    }

    /* core */
    if((xelem = ezxml_child(xml, "core")) != NULL) {
        if((xparams = ezxml_child(xelem, "settings")) != NULL) {
            for(ezxml_t xparam = ezxml_child(xparams, "param"); xparam; xparam = xparam->next) {
                const char *name = ezxml_attr(xparam, "name");
                const char *val = ezxml_attr(xparam, "value");

                if(wstk_str_is_empty(name)) { continue; }
            }
        }
    }

    /* modules */
    if((xelem = ezxml_child(xml, "modules")) != NULL) {
        for(ezxml_t xparam = ezxml_child(xelem, "module"); xparam; xparam = xparam->next) {
            const char *path = ezxml_attr(xparam, "path");
            const char *langs = ezxml_attr(xparam, "languages");
            const char *enabled = ezxml_attr(xparam, "enabled");

            if(wstk_str_is_empty(path) || wstk_str_is_empty(langs)) { continue; }

            if(wstk_str_equal(enabled, "true", false)) {
                ttsd_config_entry_module_t *entry = NULL;
                char *mod_path = NULL;

                if(path[0] == '/') {
                    mod_path = wstk_str_dup(path);
                } else {
                    status = wstk_sdprintf(&mod_path, "%s/%s", global->path_modules, path);
                    if(status != WSTK_STATUS_SUCCESS || !mod_path) { log_error("wstk_sdprintf()"); break; }
                }

                if(!wstk_file_exists(mod_path)) {
                    log_warn("File not found: %s", mod_path);
                    mod_path = wstk_mem_deref(mod_path);
                    continue;
                }

                status = wstk_mem_zalloc((void *)&entry, sizeof(ttsd_config_entry_module_t), destructor__ttsd_config_entry_module_t);
                if(status != WSTK_STATUS_SUCCESS) { log_error("wstk_mem_zalloc()"); break; }

                entry->path = mod_path;
                entry->languages = wstk_str_dup(langs);


                wstk_list_add_tail(global->modules, entry, destructor__ttsd_config_entry_module_t);
            }
        }
        if(status != WSTK_STATUS_SUCCESS) { goto out; }
    }

out:
    if(xml) {
        ezxml_free(xml);
    }
    return status;
}

