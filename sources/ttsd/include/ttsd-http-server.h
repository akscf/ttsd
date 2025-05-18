/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_HTTP_SERVER_H
#define TTSD_HTTP_SERVER_H
#include <ttsd-core.h>
#include <ttsd-config.h>

wstk_status_t ttsd_http_server_init(ttsd_global_t *global);
wstk_status_t ttsd_http_server_shutdown();
wstk_status_t ttsd_http_server_start();

#endif
