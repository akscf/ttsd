/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_CODECS_H
#define TTSD_CODECS_H
#include <ttsd-core.h>
#include <ttsd-config.h>

typedef struct {
    uint8_t     *data;
    size_t      len;
    uint8_t     type; // 0=resampler, 1=wav, 2=mp3,
} ttsd_codec_result_t;

wstk_status_t ttsd_codecs_init(ttsd_global_t *globals);
wstk_status_t ttsd_codecs_shutdown();

wstk_status_t ttsd_codec_result_alloc(ttsd_codec_result_t **out, uint8_t type, uint8_t *data, size_t data_len);
wstk_status_t ttsd_encode_wav(ttsd_codec_result_t **out, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len);
wstk_status_t ttsd_encode_mp3(ttsd_codec_result_t **out, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len);
wstk_status_t ttsd_resample(ttsd_codec_result_t **out, uint32_t to_samplerate, uint32_t samplerate, uint32_t channels, uint8_t *buf, size_t buf_len);

#endif
