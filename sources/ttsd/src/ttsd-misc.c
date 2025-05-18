/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-core.h>
#include <ttsd-synth.h>
#include <pwd.h>
#include <grp.h>

static void destructor__ttsd_synthesis_result_t(void *data) {
    ttsd_synthesis_result_t *obj = (ttsd_synthesis_result_t *)data;

    if(!obj) return;

    wstk_mem_deref(obj->file);
    wstk_mem_deref(obj->data);
}

// ---------------------------------------------------------------------------------------------------------------------
wstk_status_t ttsd_synthesis_result_allocate(ttsd_synthesis_result_t **res, uint32_t samplerate, uint32_t channels, uint32_t samples, int16_t *data) {
    wstk_status_t status = WSTK_STATUS_FALSE;
    ttsd_synthesis_result_t *res_local = NULL;

    status = wstk_mem_zalloc((void *)&res_local, sizeof(ttsd_synthesis_result_t), destructor__ttsd_synthesis_result_t);
    if(status != WSTK_STATUS_SUCCESS) { log_error("wstk_mem_zalloc()"); goto out; }

    uint32_t dlen = (samples * sizeof (int16_t));
    if(samples > 0) {
        status = wstk_mem_alloc((void *)&res_local->data, dlen, NULL);
        if(status != WSTK_STATUS_SUCCESS) { log_error("wstk_mem_zalloc()"); goto out; }
    }
    if(data && res_local->data) {
        memcpy((uint8_t *)res_local->data, (uint8_t *)data, dlen);
    }
    res_local->samples = samples;
    res_local->channels = channels;
    res_local->samplerate = samplerate;

    *res = res_local;
out:
    if(status != WSTK_STATUS_SUCCESS) {
        if(res_local) {
            wstk_mem_deref(res_local->data);
            wstk_mem_deref(res_local);
        }
    }
    return status;
}


wstk_status_t ttsd_dir_create_ifne(char *dir) {
    if(!wstk_dir_exists(dir)) {
        return wstk_dir_create(dir, true);
    }
    return WSTK_STATUS_SUCCESS;
}

wstk_status_t ttsd_switch_ug(char *user, char *group) {
    struct passwd *pw = NULL;
    struct group *gr = NULL;
    uid_t uid = 0;
    gid_t gid = 0;

    if(user) {
        if((pw = getpwnam(user)) == NULL) {
            log_error("Unknown user: %s", user);
            return WSTK_STATUS_FALSE;
        }
        uid = pw->pw_uid;
    }
    if(group) {
        if((gr = getgrnam(group)) == NULL) {
            log_error("Unknown group: %s", user);
            return WSTK_STATUS_FALSE;
        }
        gid = gr->gr_gid;
    }

    if(uid && getuid() == uid && (!gid || gid == getgid())) {
        return WSTK_STATUS_SUCCESS;
    }

    if(gid) {
        if(setgid(gid) < 0) {
            log_error("Unable to perform: setgid()");
            return WSTK_STATUS_FALSE;
        }
    } else {
        if(setgid(pw->pw_gid) < 0) {
            log_error("Unable to perform: setgid()");
            return WSTK_STATUS_FALSE;
        }
    }

    if(setuid(uid) < 0) {
        log_error("Unable to perform: setuid()\n");
        return WSTK_STATUS_FALSE;
    }

    return WSTK_STATUS_SUCCESS;
}

char *ttsd_strftime(char obuf[], uint32_t obuf_sz, time_t ts) {
    struct tm *ltm = NULL;

    ltm = localtime(&ts);
    strftime(obuf, obuf_sz, "%d-%m-%Y", ltm);

    return (char *)obuf;
}

char *ttsd_trim(char *s) {
    int i=0, j=0;
    while((s[i] == ' ')||(s[i] == '\t')) {
        i++;
    }
    if(i > 0)  {
        for(j=0; j < strlen(s); j++) {
            s[j] = s[j+i];
        }
        s[j] = '\0';
    }
    i = strlen(s)-1;
    while((s[i] == ' ')||(s[i] == '\t')) {
        i--;
    }
    if(i < (strlen(s)-1)) {
        s[i+1] = '\0';
    }
    return s;
}
