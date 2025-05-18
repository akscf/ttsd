/**
 **
 ** (C)2025 aks
 **/
#include <ttsd-core.h>
#include <ttsd-codecs.h>
#include <speex/speex_resampler.h>
#include <lame.h>
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

static bool _init = false;

static void destructor__ttsd_codec_result_t(void *data) {
    ttsd_codec_result_t *obj = (ttsd_codec_result_t *)data;

    if(!obj) return;

    if(obj->data) {
        if(obj->type == 1) {
            drwav_free(obj->data, NULL);
        } else  {
            free(obj->data);
        }
        obj->data = NULL;
    }
}

static void lame_log_error(char const *fmt, va_list ap) {
    if(fmt) {
        wstk_log_verror(fmt, ap);
    }
}
static void lame_log_debug(char const *fmt, va_list ap) {
    if(fmt) {
        wstk_log_vdebug(fmt, ap);
    }
}
static void lame_log_msg(char const *fmt, va_list ap) {
    if(fmt) {
        wstk_log_vdebug(fmt, ap);
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// public
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
wstk_status_t ttsd_codecs_init(ttsd_global_t *globals) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    int err = 0;

    if(status == WSTK_STATUS_SUCCESS) {
        _init = true;
    }

    return status;
}

wstk_status_t ttsd_codecs_shutdown() {
    if(_init) {
        _init = false;
    }

    return WSTK_STATUS_SUCCESS;
}

wstk_status_t ttsd_codec_result_alloc(ttsd_codec_result_t **out, uint8_t type, uint8_t *data, size_t data_len) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    ttsd_codec_result_t *cout = NULL;

    status = wstk_mem_zalloc((void *)&cout, sizeof(ttsd_codec_result_t), destructor__ttsd_codec_result_t);
    if(status != WSTK_STATUS_SUCCESS) { goto out; }

    cout->type = type;
    cout->data = data;
    cout->len =data_len;

out:
    if(status == WSTK_STATUS_SUCCESS) {
        *out = cout;
    } else {
        wstk_mem_deref(cout);
    }
    return status;
}

/**
 ** buf     - pcm i16 stream
 ** buf_len - in bytes
 **/
wstk_status_t ttsd_resample(ttsd_codec_result_t **out, uint32_t to_samplerate, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    ttsd_codec_result_t *out_local = NULL;
    SpeexResamplerState *srs = NULL;
    size_t dst_buf_sz = 0;
    size_t dst_smps = 0;
    uint8_t *dst_buf = NULL;
    int err = 0;

#ifdef TTSD_DEBUG
    log_debug("resampling (%u => %u) [ch=%u, len=%u]", samplerate, to_samplerate, channels, (uint32_t )buf_len);
#endif

    srs = speex_resampler_init(channels, samplerate, to_samplerate, SPEEX_RESAMPLER_QUALITY_VOIP, &err);
    if(!srs) {
        log_error("speex_resampler_init()");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    dst_buf_sz = ((uint32_t)(((float)to_samplerate / (float)samplerate) * (float)buf_len));
    dst_smps = dst_buf_sz / sizeof(int16_t);

    if(!(dst_buf = malloc(dst_buf_sz))) {
        log_error("malloc()");
        goto out;
    }

    speex_resampler_process_interleaved_int(srs, (int16_t *)buf, (uint32_t *)&buf_len, (int16_t *)dst_buf, (uint32_t *)&dst_smps);
    if((status = ttsd_codec_result_alloc(&out_local, 0, dst_buf, dst_smps * sizeof(int16_t))) != WSTK_STATUS_SUCCESS) {
        log_error("ttsd_codec_result_alloc()");
        goto out;
    }

    *out = out_local;
out:
    if(srs) {
        speex_resampler_destroy(srs);
    }
    if(status != WSTK_STATUS_SUCCESS) {
        if(out_local) { wstk_mem_deref(out_local); }
        else { if(dst_buf) free(dst_buf); }
    }
    return status;
}

/**
 ** buf     - pcm i16 stream
 ** buf_len - in bytes
 **/
wstk_status_t ttsd_encode_wav(ttsd_codec_result_t **out, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    drwav_data_format format;
    drwav owav;
    ttsd_codec_result_t *out_local = NULL;
    uint8_t *wavbuf = NULL;
    size_t wavbuf_sz = 0;

    if(!out || !buf || !buf_len) {
        return WSTK_STATUS_INVALID_PARAM;
    }

    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = samplerate;
    format.bitsPerSample = 16;

    uint32_t wsz = (buf_len / sizeof(int16_t)) / channels;

    if(!drwav_init_memory_write(&owav, (void *)&wavbuf, &wavbuf_sz, &format, NULL)) {
        log_error("drwav_init_memory_write()");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    if(drwav_write_pcm_frames(&owav, wsz, (int16_t *)buf) <= 0) {
        log_error("drwav_write_pcm_frames()");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    drwav_uninit(&owav);

    if((status = ttsd_codec_result_alloc(&out_local, 1, wavbuf, wavbuf_sz)) != WSTK_STATUS_SUCCESS) {
        log_error("ttsd_codec_result_alloc()");
        goto out;
    }

    *out = out_local;
out:
    if(status != WSTK_STATUS_SUCCESS) {
        if(out_local) { wstk_mem_deref(out_local); }
        else { if(wavbuf) drwav_free(wavbuf, NULL); }
    }

    return status;
}

/**
 ** buf     - pcm i16 stream
 ** buf_len - in bytes
 **/
wstk_status_t ttsd_encode_mp3(ttsd_codec_result_t **out, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len) {
    wstk_status_t status = WSTK_STATUS_SUCCESS;
    lame_global_flags *lgf = NULL;
    ttsd_codec_result_t *out_local = NULL;
    size_t mp3buf_sz = 0;
    uint8_t *mp3buf = NULL;
    int rlen = 0;

    if(!out || !buf || !buf_len) {
        return WSTK_STATUS_INVALID_PARAM;
    }

    if(!(lgf = lame_init())) {
        log_error("lame_init()");
        wstk_goto_status(WSTK_STATUS_FALSE, out);
    }

    id3tag_init(lgf);
    id3tag_v2_only(lgf);
    id3tag_pad_v2(lgf);

    lame_set_brate(lgf, 16 * (samplerate / 8000) * channels);
    lame_set_num_channels(lgf, channels);
    lame_set_in_samplerate(lgf, samplerate);
    lame_set_mode(lgf, channels == 2 ? STEREO : MONO);
    lame_set_quality(lgf, 2);

    lame_set_errorf(lgf, lame_log_error);
    lame_set_debugf(lgf, lame_log_debug);
    lame_set_msgf(lgf, lame_log_msg);
    //lame_print_config(lgf);
    lame_set_bWriteVbrTag(lgf, 0);
    lame_mp3_tags_fid(lgf, NULL);
    lame_init_params(lgf);

    mp3buf_sz = (buf_len + 512);
    if(!(mp3buf = malloc(mp3buf_sz))) {
        log_error("malloc()");
        goto out;
    }

    memset(mp3buf, 0x0, mp3buf_sz);
    rlen = lame_encode_buffer(lgf, (void *)buf, NULL, (int)(buf_len / sizeof(int16_t)), mp3buf, mp3buf_sz);
    //rlen = (rlen + (samplerate / 2) <= mp3buf_sz ? rlen + (samplerate / 2) : rlen);
    if((status = ttsd_codec_result_alloc(&out_local, 2, mp3buf, rlen)) != WSTK_STATUS_SUCCESS) {
        log_error("ttsd_codec_result_alloc()");
        goto out;
    }

    *out = out_local;

out:
    if(lgf) {
        lame_close(lgf);
    }
    if(status != WSTK_STATUS_SUCCESS) {
        if(out_local) { wstk_mem_deref(out_local); }
        else { if(mp3buf) free(mp3buf); }
    }
    return status;
}
