/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_CORE_H
#define TTSD_CORE_H
#include <wstk.h>

#define APP_DEFAULT_HOME    "/opt/ttsd"
#define APP_SYSLOG_NAME     "ttsd"
#define APP_VERSION_STR     "1.0.1"
#define HTTPD_IDENT         "ttsd/1.x"

//#define TTSD_DEBUG

typedef enum {
    D_ACTION_NONE = 0,
    D_ACTION_START,
    D_ACTION_STOP,
    D_ACTION_RELOAD
} daemon_action_e;

/* ttsd-main.c */
int ttsd_main(int argc, char **argv);
const char *ttsd_core_get_path_home();
const char *ttsd_core_get_path_config();
const char *ttsd_core_get_path_tmp();
const char *ttsd_core_get_path_var();
const char *ttsd_core_get_path_www();

/* ttsd-misc.c */
wstk_status_t ttsd_switch_ug(char *user, char *group);
wstk_status_t ttsd_dir_create_ifne(char *dir);
char *ttsd_strftime(char obuf[], uint32_t obuf_sz, time_t ts);
char *ttsd_trim(char *s);

#endif
