/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-core.h>
#include <ttsd-config.h>
#include <ttsd-modules.h>
#include <ttsd-http-server.h>
#include <ttsd-codecs.h>

static ttsd_global_t *ttsd_global;

static void signal_handler(int sig) {
    if(ttsd_global->fl_shutdown)  {
        return;
    }

    switch (sig) {
        case SIGHUP:  break;
        case SIGALRM: break;
        case SIGINT:
        case SIGTERM:
            ttsd_global->fl_ready = false;
            break;
    }
}

static void usage(void) {
    printf("Usage: ttsd [options]\n"
        "options:\n"
        "\t-h <path>        app home (default: %s)\n"
        "\t-a <action>      start | start-debug | stop | reload\n"
        "\t-u username      start daemon with a specified user\n"
        "\t-g groupname     start daemon with a specified group\n"
        "\t?                this help\n",
        APP_DEFAULT_HOME
    );
    exit(1);
}

// ----------------------------------------------------------------------------------------------------------------------------
// main
// ----------------------------------------------------------------------------------------------------------------------------
#define main_mem_fail() fprintf(stderr, "ERROR: Unable to allocate memory!\n");
#define main_goto_err(_status, _label) err = _status; goto _label

const char *ttsd_core_get_path_home() {
    return ttsd_global->path_home;
}
const char *ttsd_core_get_path_config() {
    return ttsd_global->path_config;
}
const char *ttsd_core_get_path_tmp() {
    return ttsd_global->path_tmp;
}
const char *ttsd_core_get_path_var() {
    return ttsd_global->path_var;
}

int ttsd_main(int argc, char **argv) {
    daemon_action_e action = D_ACTION_NONE;
    char *xuser = NULL, *xgroup = NULL;
    pid_t srv_pid = 0;
    int opt = 0, err = 0;
    bool fl_del_pid = false;

    if(wstk_core_init() != WSTK_STATUS_SUCCESS) {
        fprintf(stderr, "ERROR: wstk_core_init() failed\n");
        exit(1);
    }

    if(ttsd_global_init(&ttsd_global) != WSTK_STATUS_SUCCESS) {
        fprintf(stderr, "ERROR: ttsd_global_init() failed\n");
        exit(1);
    }

    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    while((opt = getopt(argc, argv, "a:h:r:u:g:?")) != -1) {
        switch(opt) {
            case 'h':
                if(wstk_str_dup2(&ttsd_global->path_home, optarg) != WSTK_STATUS_SUCCESS) {
                    main_mem_fail();
                    main_goto_err(-1, out);
                }
            break;
            case 'u':
                if(wstk_str_dup2(&xuser, optarg) != WSTK_STATUS_SUCCESS) {
                    main_mem_fail();
                    main_goto_err(-1, out);
                }
            break;
            case 'g':
                if(wstk_str_dup2(&xgroup, optarg) != WSTK_STATUS_SUCCESS) {
                    main_mem_fail();
                    main_goto_err(-1, out);
                }
            break;
            case 'a':
                if(wstk_str_equal(optarg, "start", false)) {
                    action = D_ACTION_START;
                } else if(wstk_str_equal(optarg, "start-debug", false)) {
                    action = D_ACTION_START;
                    ttsd_global->fl_debug_mode = true;
                } else if(wstk_str_equal(optarg, "stop", false)) {
                    action = D_ACTION_STOP;
                } else if(wstk_str_equal(optarg, "reload", false)) {
                    action = D_ACTION_RELOAD;
                }
            break;
            case '?':
                action = D_ACTION_NONE;
            break;
        }
    }

    if(action == D_ACTION_NONE) {
        usage();
        main_goto_err(0, out);
    }

    if(xuser || xgroup) {
        if(ttsd_switch_ug(xuser, xgroup) != WSTK_STATUS_SUCCESS) {
            fprintf(stderr, "ERROR: Unable to switch user/group!\n");
            main_goto_err(-1, out);
        }
    }

    if(ttsd_global->path_home == NULL) {
        if(wstk_str_dup2(&ttsd_global->path_home, APP_DEFAULT_HOME) != WSTK_STATUS_SUCCESS) {
            main_mem_fail();
            main_goto_err(-1, out);
        }
    }

    if(wstk_sdprintf(&ttsd_global->path_config, "%s/configs", ttsd_global->path_home) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->path_models, "%s/models", ttsd_global->path_home) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->path_modules, "%s/lib/mods", ttsd_global->path_home) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->path_var, "%s/var", ttsd_global->path_home) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->file_config, "%s/ttsd-conf.xml", ttsd_global->path_config) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->file_pid, "%s/ttsd", ttsd_global->path_var) != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }
    if(wstk_sdprintf(&ttsd_global->path_tmp, "/tmp") != WSTK_STATUS_SUCCESS) {
        main_mem_fail();
        main_goto_err(-1, out);
    }

    /* D_ACTION_STOP */
    if(action == D_ACTION_STOP || action == D_ACTION_RELOAD)  {
        srv_pid = wstk_pid_read(ttsd_global->file_pid);
        if(!srv_pid) {
            fprintf(stderr,"ERROR: Server is not running\n");
            main_goto_err(-1, out);
        }
        if(kill(srv_pid, (action == D_ACTION_RELOAD ? SIGHUP : SIGTERM)) != 0) {
            fprintf(stderr,"ERROR: Couldn't stop the server (pid=%d)\n", srv_pid);
            wstk_pid_delete(ttsd_global->file_pid);
            main_goto_err(-1, out);
        }
        goto out;
    }

    /* D_ACTION_START */
    if(ttsd_dir_create_ifne(ttsd_global->path_home) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_dir_create_ifne(ttsd_global->path_config) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_dir_create_ifne(ttsd_global->path_models) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_dir_create_ifne(ttsd_global->path_modules) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_dir_create_ifne(ttsd_global->path_var) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_dir_create_ifne(ttsd_global->path_tmp) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }

    srv_pid = wstk_pid_read(ttsd_global->file_pid);
    if(srv_pid && srv_pid != getpid()) {
        fprintf(stderr,"ERROR: Server already running (pid=%d)\n", srv_pid);
        main_goto_err(-1, out);
    }

    if(!ttsd_global->fl_debug_mode) {
        if(wstk_demonize(NULL) != WSTK_STATUS_SUCCESS) {
            fprintf(stderr,"ERROR: Couldn't daemonize!\n");
            main_goto_err(-1, out);
        }
    } else {
        fprintf(stderr,"*** debug mode, use ctrl+c to stop ***\n");
    }

    /* configuring */
    if(!ttsd_global->fl_debug_mode) {
        wstk_log_configure(WSTK_LOG_SYSLOG, APP_SYSLOG_NAME);
    }

    if(ttsd_config_load(ttsd_global) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }

    srv_pid = getpid();
    if(wstk_pid_write(ttsd_global->file_pid, srv_pid) != WSTK_STATUS_SUCCESS) {
        log_error("Unable to create server pid: %s", ttsd_global->file_pid);
        main_goto_err(-1, out);
    }
    fl_del_pid = true;

    /* signals */
    signal(SIGINT, signal_handler);
    signal(SIGHUP, signal_handler);
    signal(SIGALRM, signal_handler);
    signal(SIGTERM, signal_handler);

    /* services */
    if(ttsd_codecs_init(ttsd_global) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_http_server_init(ttsd_global) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_modules_manager_init(ttsd_global) != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }
    if(ttsd_http_server_start() != WSTK_STATUS_SUCCESS) {
        main_goto_err(-1, out);
    }

    /* mian loop */
    ttsd_global->fl_ready = true;
    ttsd_global->fl_shutdown = false;

    log_notice("ttsd-%s [pid:%d / uid:%d / gid:%d / Opensouce] - started", APP_VERSION_STR, srv_pid, getuid(), getgid());

    while(ttsd_global->fl_ready) {
        WSTK_SCHED_YIELD(1000);
    }

    ttsd_global->fl_ready = false;
    ttsd_global->fl_shutdown = true;

    /* stop services */
    ttsd_http_server_shutdown();
    ttsd_modules_manager_shutdown();
    ttsd_codecs_shutdown();

out:
    if(fl_del_pid) {
        wstk_pid_delete(ttsd_global->file_pid);
    }

    wstk_mem_deref(ttsd_global);
    wstk_mem_deref(xuser);
    wstk_mem_deref(xgroup);

    if(action == D_ACTION_START) {
        if(!err) {
            log_notice("ttsd-%s [pid:%d] - stopped", APP_VERSION_STR, srv_pid);
        }
    }

    wstk_core_shutdown();
    return err;
}

