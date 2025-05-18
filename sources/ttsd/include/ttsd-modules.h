/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_MODULES_H
#define TTSD_MODULES_H
#include <ttsd-core.h>
#include <ttsd-config.h>
#include <ttsd-synth.h>

wstk_status_t ttsd_modules_manager_init(ttsd_global_t *global);
wstk_status_t ttsd_modules_manager_shutdown();

bool ttsd_module_exists(const char *name);
bool ttsd_language_exists(const char *name);
wstk_status_t ttsd_synthesis(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params);


typedef void (ttsd_mod_unload_t)(void);
typedef wstk_status_t (ttsd_mod_load_t)(void);
typedef wstk_status_t (ttsd_mod_synthesize_t)(ttsd_synthesis_result_t **result, ttsd_synthesis_params_t *params);

typedef struct {
    const char              *name;
    ttsd_mod_load_t         *load;
    ttsd_mod_unload_t       *unload;
    ttsd_mod_synthesize_t   *synthesize;
} ttsd_module_interface_t;

#define TTSD_MODULE_DEFINITION(name, load, unload, synthesize) \
 ttsd_module_interface_t ttsd_module_interface_v1 = { #name, load, unload, synthesize }



#endif
