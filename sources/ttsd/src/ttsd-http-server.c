/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-http-server.h>
#include <ttsd-modules.h>
#include <ttsd-config.h>
#include <ttsd-codecs.h>

#define TTSD_CHARSET            "UTF-8"
#define IO_BUF_SZ               4096
#define TTSD_MAX_CONTENT_LEN    65535

typedef struct {
    ttsd_global_t                   *ttsd_glb;
    wstk_httpd_t                    *httpd;
    wstk_sockaddr_t                 laddr;
    bool                            fl_destroyed;
} ttsd_http_server_manager_t;

typedef struct {
    size_t      len;
    size_t      pos;
    uint8_t     *data;
} mem_io_conf_t;

static ttsd_http_server_manager_t *manager = NULL;

// --------------------------------------------------------------------------------------------------------------
static void destructor__ttsd_http_server_manager_t(void *data) {
    ttsd_http_server_manager_t *obj = (ttsd_http_server_manager_t *)data;

    if(!obj || obj->fl_destroyed) {
        return;
    }
    obj->fl_destroyed = true;

#ifdef TTSD_DEBUG
    log_debug("stopping web-service...");
#endif

    if(obj->httpd) {
        obj->httpd = wstk_mem_deref(obj->httpd);
    }

}

static void ttsd_auth_handler(wstk_httpd_auth_request_t *req, wstk_httpd_auth_response_t *rsp) {
    bool permitted = false;

    if(!manager || manager->fl_destroyed)  {
        return;
    }

    if(wstk_str_is_empty(manager->ttsd_glb->http_server->secret)) {
        permitted = true;
    } else {
        permitted = (req->token && wstk_str_equal(manager->ttsd_glb->http_server->secret, req->token, true));
    }

    rsp->permitted = permitted;
}

static const char *fmt2ctype(const char *name) {
    if(wstk_str_is_empty(name)) {
        return "application/octet-stream";
    }
    if(wstk_str_equal(name, "wav", false)) {
        return "audio/wav";
    }
    if(wstk_str_equal(name, "mp3", false)) {
        return "audio/mp3";
    }
    if(wstk_str_equal(name, "l16", false)) {
        return "audio/l16";
    }
    return "application/octet-stream";
}

static wstk_status_t mem_io_read(void *udata, wstk_mbuf_t *buf) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    mem_io_conf_t *io_conf = (mem_io_conf_t *)udata;

    if(!io_conf->len || io_conf->pos >= io_conf->len) {
        status = WSTK_STATUS_NODATA;
    } else {
        uint32_t sz = (IO_BUF_SZ > io_conf->len ? io_conf->len : IO_BUF_SZ);
        if(io_conf->pos + sz > io_conf->len) { sz = (io_conf->len - io_conf->pos); }

        wstk_mbuf_clean(buf);
        status = wstk_mbuf_write_mem(buf, io_conf->data + io_conf->pos, sz);
        io_conf->pos += sz;
    }

    return status;
}

static void speech_http_handler(wstk_http_conn_t *conn, wstk_http_msg_t *msg, void *udata) {
    wstk_httpd_sec_ctx_t sec_ctx = {0};
    wstk_status_t status = 0;
    wstk_mbuf_t *body_tmp_buf = NULL;
    ttsd_synthesis_params_t params = { 0 };
    ttsd_synthesis_result_t *synth_result = NULL;
    ttsd_codec_result_t *resamples_result = NULL;
    cJSON *jparams = NULL;
    char *body_ptr = NULL;
    size_t body_len = 0;

    if(!manager || manager->fl_destroyed)  {
        return;
    }

    if(msg->clen > TTSD_MAX_CONTENT_LEN) {
        wstk_httpd_ereply(conn, 400, "Content is too big");
        return;
    }

    if(wstk_pl_strcasecmp(&msg->method, "post") != 0) {
        wstk_httpd_ereply(conn, 400, "Bad request (expected method: POST)");
        return;
    }

    if(!wstk_pl_strstr(&msg->ctype, "application/json")) {
        wstk_httpd_ereply(conn, 400, "Bad request (expected type: application/json)");
        return;
    }

    /* authenticate */
    wstk_httpd_autheticate(conn, msg, &sec_ctx);
    if(!sec_ctx.permitted) {
        wstk_httpd_ereply(conn, 401, "Access not allowed");
        return;
    }

    /* load the rest of the content */
    if(msg->clen > wstk_mbuf_left(conn->buffer)) {
        if(wstk_mbuf_alloc(&body_tmp_buf, msg->clen) != WSTK_STATUS_SUCCESS) {
            log_error("Unable to allocate memory");
            wstk_httpd_ereply(conn, 500, NULL);
            goto out;
        }
        if(wstk_httpd_conn_rdlock(conn, true) == WSTK_STATUS_SUCCESS) {
            while(true) {
                status = wstk_httpd_read(conn, body_tmp_buf, WSTK_RD_TIMEOUT(msg->clen));
                if(manager->fl_destroyed || body_tmp_buf->pos >= msg->clen)  {
                    break;
                }
                if(status == WSTK_STATUS_CONN_DISCON || status == WSTK_STATUS_CONN_EXPIRE)  {
                    break;
                }
            }
            wstk_httpd_conn_rdlock(conn, false);
        } else {
            status = WSTK_STATUS_LOCK_FAIL;
            log_error("Unable to lock connection (rdlock)");
        }
        if(!WSTK_RW_ACCEPTABLE(status)) {
            log_error("Unable to read the whole body (status=%d)", (int)status);
            wstk_httpd_ereply(conn, 500, NULL);
            goto out;
        }
        body_ptr = (char *)body_tmp_buf->buf;
        body_len = body_tmp_buf->end;
    } else {
        body_ptr = (char *)wstk_mbuf_buf(conn->buffer);
        body_len = wstk_mbuf_left(conn->buffer);
    }

    if(!body_len) {
        log_error("Empty content (body_len == 0)");
        wstk_httpd_ereply(conn, 400, "Empty content");
        goto out;
    }

    /* parse request */
    if((jparams = cJSON_ParseWithLength(body_ptr, body_len))) {
        const char *lang_ref = NULL, *format_ref = NULL, *text_ref = NULL;
        uint32_t srate = 0, channels = 1;
        cJSON *jlang = cJSON_GetObjectItem(jparams, "language");
        cJSON *jsrate = cJSON_GetObjectItem(jparams, "samplerate");
        //cJSON *jchnls = cJSON_GetObjectItem(jparams, "channels");
        cJSON *jfmout = cJSON_GetObjectItem(jparams, "format");
        cJSON *jtext = cJSON_GetObjectItem(jparams, "input");

        if(jlang && cJSON_IsString(jlang))  {
            lang_ref = cJSON_GetStringValue(jlang);
        }
        if(jfmout && cJSON_IsString(jfmout))  {
            format_ref = cJSON_GetStringValue(jfmout);
        } else {
            format_ref = "l16";
        }
        if(jtext && cJSON_IsString(jtext))  {
            text_ref = cJSON_GetStringValue(jtext);
        }
        if(jsrate && cJSON_IsNumber(jsrate))  {
            srate = (uint32_t) cJSON_GetNumberValue(jsrate);
        }

        if(wstk_str_is_empty(lang_ref) || wstk_str_is_empty(format_ref) || wstk_str_is_empty(text_ref) || !srate) {
            log_error("Missing one or more required parameters (language|samplerate|format|input)");
            wstk_httpd_ereply(conn, 400, "Missing required parameters");
            goto out;
        }
        if(!wstk_str_equal(format_ref, "wav", false) && !wstk_str_equal(format_ref, "mp3", false) && !wstk_str_equal(format_ref, "l16", false)) {
            wstk_httpd_ereply(conn, 400, "Unsupported output format (allowed: wav|mp3|l16)");
            goto out;
        }

#ifdef TTSD_DEBUG
        log_debug("language=[%s], format=[%s], samplerate=[%d], channels=[%d], text=[%s]", lang_ref, format_ref, srate, channels, text_ref);
#endif

        params.channels = channels;
        params.samplerate = srate;
        params.format = (char *)format_ref;
        params.language = (char *)lang_ref;
        params.text = (char *)text_ref;

        if(ttsd_synthesis(&synth_result, &params) != WSTK_STATUS_SUCCESS) {
            if(status == WSTK_STATUS_NOT_FOUND) {
                wstk_httpd_ereply(conn, 400, "Unsupported language");
            } else {
                wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
            }
            goto out;
        }
        if(!synth_result) {
            log_error("synth_result == NULL");
            wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
            goto out;
        }
        if(synth_result->file) {
            if(wstk_file_exists(synth_result->file)) {
                wstk_status_t st = 0;
                wstk_file_t blobf = { 0 };
                wstk_file_meta_t file_meta = { 0 };
                const char *ctype = fmt2ctype(synth_result->file);

                if(wstk_file_get_meta(synth_result->file, &file_meta) != WSTK_STATUS_SUCCESS) {
                    log_error("Unable to read file meta (%s)", synth_result->file);
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                    goto out;
                }

                if(wstk_file_open(&blobf, synth_result->file, "rb") != WSTK_STATUS_SUCCESS) {
                    log_error("Unable to open file (%s)", synth_result->file);
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                    goto out;
                }

                if(wstk_httpd_breply(conn, 200, NULL, ctype, file_meta.size, file_meta.mtime, (wstk_httpd_blob_reader_callback_t)wstk_file_read, (void *)&blobf) != WSTK_STATUS_SUCCESS) {
                    log_error("Unable to read file (%s)", synth_result->file);
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                }

                wstk_file_close(&blobf);
            } else {
                log_error("File not found (%s)", synth_result->file);
                wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
            }
            goto out;
        }
        if(synth_result->data && synth_result->samples) {
            const char *ctype = fmt2ctype(format_ref);
            uint8_t *dptr = NULL;
            uint32_t dchannels = synth_result->channels;
            uint32_t dsamplerate = synth_result->samplerate;
            size_t dlen = synth_result->samples * sizeof(int16_t);

            if(params.samplerate != synth_result->samplerate)  {
                status = ttsd_resample(&resamples_result, params.samplerate, dsamplerate, dchannels, (uint8_t *)synth_result->data, dlen);
                if(status != WSTK_STATUS_SUCCESS || !resamples_result) {
                    log_error("Resampling failed");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                    goto out;
                }

                dptr = resamples_result->data;
                dlen = resamples_result->len;
                dsamplerate = params.samplerate;
            }

            if(wstk_str_equal(format_ref, "wav", false)) {
                mem_io_conf_t io_conf = { 0 };
                ttsd_codec_result_t *cout = NULL;
                time_t ts = wstk_time_epoch_now();

                if(ttsd_encode_wav(&cout, dsamplerate, dchannels, dptr, dlen) != WSTK_STATUS_SUCCESS || !cout) {
                    log_error("Encoding failed (VAW)");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                    goto out;
                }

                io_conf.len = cout->len;
                io_conf.data = cout->data;

                if(wstk_httpd_breply(conn, 200, NULL, fmt2ctype(format_ref), io_conf.len, ts, (wstk_httpd_blob_reader_callback_t)mem_io_read, (void *)&io_conf) != WSTK_STATUS_SUCCESS) {
                    log_error("wstk_httpd_breply()");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                }

                wstk_mem_deref(cout);

            } else if(wstk_str_equal(format_ref, "mp3", false)) {
                mem_io_conf_t io_conf = { 0 };
                ttsd_codec_result_t *cout = NULL;
                time_t ts = wstk_time_epoch_now();

                if(ttsd_encode_mp3(&cout, dsamplerate, dchannels, dptr, dlen) != WSTK_STATUS_SUCCESS || !cout) {
                    log_error("Encoding failed (MP3)");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                    goto out;
                }

                io_conf.len = cout->len;
                io_conf.data = cout->data;

                if(wstk_httpd_breply(conn, 200, NULL, fmt2ctype(format_ref), io_conf.len, ts, (wstk_httpd_blob_reader_callback_t)mem_io_read, (void *)&io_conf) != WSTK_STATUS_SUCCESS) {
                    log_error("wstk_httpd_breply()");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                }

                wstk_mem_deref(cout);

            } else if(wstk_str_equal(format_ref, "l16", false)) {
                mem_io_conf_t io_conf = { 0 };
                time_t ts = wstk_time_epoch_now();

                io_conf.len = dlen;
                io_conf.data = dptr;

                if(wstk_httpd_breply(conn, 200, NULL, fmt2ctype(format_ref), io_conf.len, ts, (wstk_httpd_blob_reader_callback_t)mem_io_read, (void *)&io_conf) != WSTK_STATUS_SUCCESS) {
                    log_error("wstk_httpd_breply()");
                    wstk_httpd_ereply(conn, 500, "Internal server error (see log)");
                }
            }
        }
        goto out;
    }

    log_error("Unable to parse json parameters");
    wstk_httpd_ereply(conn, 400, "Bad request (unable to parse parameters)");
out:
    if(jparams) cJSON_Delete(jparams);
    wstk_mem_deref(synth_result);
    wstk_mem_deref(body_tmp_buf);
    wstk_mem_deref(resamples_result);
    wstk_httpd_sec_ctx_clean(&sec_ctx);
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// public
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
wstk_status_t ttsd_http_server_init(ttsd_global_t *ttsd_glb) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    const ttsd_config_entry_http_server_t *http_server = ttsd_glb->http_server;

    status = wstk_mem_zalloc((void *)&manager, sizeof(ttsd_http_server_manager_t), destructor__ttsd_http_server_manager_t);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    manager->ttsd_glb = ttsd_glb;

#ifdef TTSD_DEBUG
    log_debug("configuring web-service...");
#endif

    if(wstk_str_is_empty(http_server->address)) {
        log_error("Invalid web-service setting: address (%s)", http_server->address);
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }
    if(http_server->port <= 0) {
        log_error("Invalid web-service setting: port (%d)", http_server->port);
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    status = wstk_sa_set_str(&manager->laddr, http_server->address, http_server->port);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unbale to parse listening address");
        goto out;
    }

    status = wstk_httpd_create(&manager->httpd, &manager->laddr, http_server->max_conns, http_server->max_idle, TTSD_CHARSET, NULL, NULL, false);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unable to create httpd instance (%d)", (int)status);
        goto out;
    }

    status = wstk_httpd_set_ident(manager->httpd, HTTPD_IDENT);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unable to set server ident");
        goto out;
    }

    status = wstk_httpd_set_authenticator(manager->httpd, ttsd_auth_handler, true);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unable to set authenticator");
        goto out;
    }

    if(status == WSTK_STATUS_SUCCESS) {
        log_notice("Services available on: %s:%d", http_server->address, http_server->port);
    }

out:
    return status;
}

wstk_status_t ttsd_http_server_start() {
    wstk_status_t status = WSTK_STATUS_SUCCESS;

    if(!manager || manager->fl_destroyed) {
        return WSTK_STATUS_DESTROYED;
    }

    status = wstk_httpd_start(manager->httpd);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unable to start httpd (%d)", (int)status);
	return status;
    }

    status = wstk_httpd_register_servlet(manager->httpd, "/v1/speech", speech_http_handler, NULL, false);
    if(status != WSTK_STATUS_SUCCESS) {
        log_error("Unable to register servlet: /v1/speech/");
        return status;
    }

    return status;
}

wstk_status_t ttsd_http_server_shutdown() {
    wstk_status_t status = WSTK_STATUS_SUCCESS;

    if(!manager || manager->fl_destroyed) {
        return WSTK_STATUS_SUCCESS;
    }

    wstk_mem_deref(manager);

    return status;
}
