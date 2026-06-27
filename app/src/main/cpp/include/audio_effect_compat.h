/*
 * audio_effect_compat.h
 *
 * Reemplazo local de <hardware/audio_effect.h>, eliminado del NDK r25+.
 * Todos los layouts de struct están verificados contra el código fuente AOSP
 * (platform/hardware/libhardware) en la rama correspondiente a API 29-34.
 * Un solo byte fuera de lugar en cualquier struct corrompería la vtable que
 * audioserver llama mediante dlopen().
 *
 * Valores de flags: AOSP platform/hardware/libhardware/include/hardware/audio_effect.h
 * commit estable para Android 10–14 (API 29–34).
 *
 * © 2025 GORE TNS — Luis Uriel Pimentel Pérez.
 */
#pragma once
#ifndef AUDIO_EFFECT_COMPAT_H
#define AUDIO_EFFECT_COMPAT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── UUID (128 bits, layout IEEE 11578) ─────────────────────────────────────*/
typedef struct effect_uuid_s {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint16_t clockSeq;
    uint8_t  node[6];
} effect_uuid_t;

#define EFFECT_STRING_LEN_MAX 64

/* ── Descriptor del efecto ───────────────────────────────────────────────────*/
typedef struct effect_descriptor_s {
    effect_uuid_t type;
    effect_uuid_t uuid;
    uint32_t      apiVersion;
    uint32_t      flags;
    uint16_t      cpuLoad;
    uint16_t      memoryUsage;
    char          name[EFFECT_STRING_LEN_MAX];
    char          implementor[EFFECT_STRING_LEN_MAX];
} effect_descriptor_t;

/* ── Buffer de audio ─────────────────────────────────────────────────────────*/
typedef struct audio_buffer_s {
    size_t frameCount;
    union {
        void    *raw;
        int32_t *s32;
        int16_t *s16;
        uint8_t *u8;
        float   *f32;
    };
} audio_buffer_t;

/* ── Buffer config (por instancia de efecto) ─────────────────────────────────*/
typedef struct buffer_provider_s {
    int32_t (*getBuffer)(void *cookie, audio_buffer_t *buffer);
    int32_t (*releaseBuffer)(void *cookie, audio_buffer_t *buffer);
    void *cookie;
} buffer_provider_t;

typedef struct buffer_config_s {
    audio_buffer_t   buffer;
    uint32_t         samplingRate;
    uint32_t         channels;
    buffer_provider_t bufferProvider;
    uint8_t          format;
    uint8_t          accessMode;
    uint16_t         mask;
} buffer_config_t;

typedef struct effect_config_s {
    buffer_config_t inputCfg;
    buffer_config_t outputCfg;
} effect_config_t;

/* ── Interfaz vtable (primer campo de cada instancia de efecto) ──────────────*/
typedef struct effect_interface_s **effect_handle_t;

struct effect_interface_s {
    int32_t (*process)(effect_handle_t self,
                       audio_buffer_t *inBuf, audio_buffer_t *outBuf);
    int32_t (*command)(effect_handle_t self,
                       uint32_t cmdCode, uint32_t cmdSize, void *pCmdData,
                       uint32_t *replySize, void *pReplyData);
    int32_t (*get_descriptor)(effect_handle_t self,
                              effect_descriptor_t *pDescriptor);
    int32_t (*process_reverse)(effect_handle_t self,
                               audio_buffer_t *inBuf, audio_buffer_t *outBuf);
};

/* ── Comandos (enum value == índice real en AOSP API 29+) ────────────────────*/
enum effect_command_e {
    EFFECT_CMD_INIT             = 0,
    EFFECT_CMD_SET_CONFIG       = 2,   /* también llamado CONFIGURE en versiones antiguas */
    EFFECT_CMD_RESET            = 3,
    EFFECT_CMD_ENABLE           = 4,
    EFFECT_CMD_DISABLE          = 5,
    EFFECT_CMD_SET_PARAM        = 6,
    EFFECT_CMD_SET_PARAM_DEFERRED = 7,
    EFFECT_CMD_SET_PARAM_COMMIT = 8,
    EFFECT_CMD_GET_PARAM        = 9,
    EFFECT_CMD_SET_DEVICE       = 10,
    EFFECT_CMD_SET_VOLUME       = 11,
    EFFECT_CMD_SET_AUDIO_MODE   = 12,
    EFFECT_CMD_FIRST_PROPRIETARY = 0x10000
};

/* Alias de compatibilidad — omega_effect.cpp usa CONFIGURE, el valor es 2 */
#define EFFECT_CMD_CONFIGURE    EFFECT_CMD_SET_CONFIG

/* ── Flags de descriptor ─────────────────────────────────────────────────────
 * Bits 0-3: tipo de efecto.  Bits 4-6: posición de inserción.
 * Fuente: AOSP hardware/libhardware/include/hardware/audio_effect.h API 29. */
#define EFFECT_FLAG_TYPE_MASK       0x0000000fU
#define EFFECT_FLAG_TYPE_INSERT     0x00000000U   /* INSERT = 0 en API 29+   */
#define EFFECT_FLAG_TYPE_AUXILIARY  0x00000001U
#define EFFECT_FLAG_TYPE_REPLACE    0x00000002U
#define EFFECT_FLAG_TYPE_PRE_PROC   0x00000003U
#define EFFECT_FLAG_TYPE_POST_PROC  0x00000004U

#define EFFECT_FLAG_INSERT_MASK     0x00000070U
#define EFFECT_FLAG_INSERT_ANY      0x00000000U
#define EFFECT_FLAG_INSERT_FIRST    0x00000010U
#define EFFECT_FLAG_INSERT_LAST     0x00000020U
#define EFFECT_FLAG_INSERT_EXCLUSIVE 0x00000030U

#define EFFECT_FLAG_VOLUME_CTRL     0x00000400U
#define EFFECT_FLAG_DEVICE_IND      0x00000800U
#define EFFECT_FLAG_AUDIO_MODE_IND  0x00080000U

/* ── Versiones de API ────────────────────────────────────────────────────────*/
#define EFFECT_MAKE_API_VERSION(M, m) (((M) << 16) | ((m) & 0xFFFFu))
#define EFFECT_CONTROL_API_VERSION    EFFECT_MAKE_API_VERSION(2, 0)
#define EFFECT_LIBRARY_API_VERSION    EFFECT_MAKE_API_VERSION(3, 0)

/* ── Tabla de la librería (dlopen entry point) ───────────────────────────────*/
#define AUDIO_EFFECT_LIBRARY_TAG \
    ((uint32_t)('A') | ((uint32_t)('E') << 8) | ((uint32_t)('L') << 16) | ((uint32_t)('T') << 24))

typedef struct audio_effect_library_s {
    uint32_t    tag;
    uint32_t    version;
    const char *name;
    const char *implementor;
    int32_t (*query_num_effects)(uint32_t *pNumEffects);
    int32_t (*query_effect)(uint32_t index, effect_descriptor_t *pDescriptor);
    int32_t (*create_effect)(const effect_uuid_t *uuid,
                             int32_t sessionId, int32_t ioId,
                             effect_handle_t *pHandle);
    int32_t (*release_effect)(effect_handle_t handle);
    int32_t (*get_descriptor)(const effect_uuid_t *uuid,
                              effect_descriptor_t *pDescriptor);
} audio_effect_library_t;

/* UUID nulo de conveniencia */
static const effect_uuid_t EFFECT_UUID_NULL_VAL = {0, 0, 0, 0, {0,0,0,0,0,0}};
#define EFFECT_UUID_NULL (&EFFECT_UUID_NULL_VAL)

/* ── Tipos de audio (los que usa omega_effect.cpp) ──────────────────────────*/
#ifndef AUDIO_CHANNEL_OUT_STEREO
#define AUDIO_CHANNEL_OUT_STEREO 0x00000003u
#endif
#ifndef AUDIO_FORMAT_PCM_FLOAT
#define AUDIO_FORMAT_PCM_FLOAT   0x00000005u
#endif
#ifndef AUDIO_FORMAT_PCM_16_BIT
#define AUDIO_FORMAT_PCM_16_BIT  0x00000001u
#endif

typedef uint32_t audio_channel_mask_t;
typedef uint32_t audio_format_t;

static inline int audio_channel_count_from_out_mask(uint32_t mask) {
    int n = 0;
    while (mask) { n += (int)(mask & 1u); mask >>= 1u; }
    return n;
}

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_EFFECT_COMPAT_H */
