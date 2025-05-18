/**
 **
 ** (C)2025 aks
 **/
#ifndef TTSD_SPEECH_H
#define TTSD_SPEECH_H
#include <ttsd-core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        *file;
    int16_t     *data;
    uint32_t    samples;
    uint32_t    samplerate;
    uint32_t    channels;
} ttsd_synthesis_result_t;

typedef struct {
    char        *language;
    char        *format;
    char        *text;
    uint32_t    samplerate;
    uint32_t    channels;
} ttsd_synthesis_params_t;

/* ttsd-misc.c */
wstk_status_t ttsd_synthesis_result_allocate(ttsd_synthesis_result_t **res, uint32_t samplerate, uint32_t channels, uint32_t samples, int16_t *data);

#ifdef __cplusplus
}
#endif

#endif
