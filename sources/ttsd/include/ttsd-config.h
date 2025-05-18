/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_CONFIG_H
#define TTSD_CONFIG_H
#include <ttsd-core.h>

#define CONFIG_VERSION     0x1

typedef struct {
    char            *secret;
    char            *address;
    uint32_t        port;
    uint32_t        max_conns;
    uint32_t        max_idle;
} ttsd_config_entry_http_server_t;

typedef struct {
    char            *path;
    char            *languages;
} ttsd_config_entry_module_t;

typedef struct {
    wstk_mutex_t    *mutex;
    wstk_list_t     *modules;
    char            *path_home;         // $home
    char            *path_config;       // $home/configs
    char            *path_models;       // $home/models
    char            *path_modules;      // $home/moduels
    char            *path_var;          // $home/var
    char            *path_tmp;          // /tmp
    char            *file_config;       // $home/configs/ttsd-conf.xml
    char            *file_pid;          // $home/var/ttsd.pid
    bool            fl_ready;
    bool            fl_shutdown;
    bool            fl_debug_mode;
    //
    ttsd_config_entry_http_server_t     *http_server;
} ttsd_global_t;

wstk_status_t ttsd_global_init(ttsd_global_t **global);
wstk_status_t ttsd_config_load(ttsd_global_t *global);

#endif
