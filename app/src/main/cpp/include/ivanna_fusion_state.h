// ivanna_fusion_state.h — UN solo estado compartido entre el CEREBRO
// (daemon v2, fuera del HAL) y el MÚSCULO (ASRC v6, dentro del HAL).
//
// Fusión HOMOGÉNEA, no 1+1: un solo struct en SHM, una sola fuente de verdad,
// un solo bucle (músculo mide → cerebro calcula → músculo aplica).
// Ninguno funciona solo: el músculo sin cerebro es passthrough; el cerebro
// sin músculo no tiene nada que corregir.
//
// Escritores:
//   - El MÚSCULO escribe: mediciones de reloj, frames, tasa/formato del stream,
//     salud del músculo, contadores.
//   - El CEREBRO escribe: sessionId, ratio, deriva, fuente de reloj, veredicto,
//     salud del cerebro.
// Ambos leen todo. El seqlock hace la lectura atómica (el lector reintenta
// si el escritor estaba a mitad de una actualización).
//
// Fail-closed: si magic/version no coinciden, el lector trata el estado como
// inválido y el músculo pasa a passthrough. Si el ratio está viejo (>100 ms),
// el músculo pasa a passthrough.

#ifndef IVANNA_FUSION_STATE_H
#define IVANNA_FUSION_STATE_H

// Compatibilidad C y C++: el seqlock usa los builtins __atomic_* de
// GCC/Clang (idénticos en C y en C++ con el NDK), NO <stdatomic.h>
// (_Atomic no existe en C++ y atomic_load_explicit necesita std::).
// `sequence` es un uint32_t plano y SOLO se toca vía __atomic_*.
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IVANNA_FUSION_MAGIC 0x4956414Eu  // "IVAN"
#define IVANNA_FUSION_VERSION 1u
#define IVANNA_FUSION_SHM_PATH "/data/adb/ivanna_omega/ivanna_fusion_state"

// Fuente de reloj activa (la decide el cerebro).
#define IVANNA_CLOCK_MMAP 0u        // timestamp del kernel (HWSYNC/mmap)
#define IVANNA_CLOCK_WRITE_TIMING 1u // write-timing (reloj de escritura)
#define IVANNA_CLOCK_NONE 2u        // sin reloj válido (bypass)

// Veredicto de la fusión (lo decide el cerebro).
#define IVANNA_VERDICT_LOADED 0u   // enganchado y corrigiendo
#define IVANNA_VERDICT_BYPASS 1u   // passthrough (kill switch o sin cerebro)
#define IVANNA_VERDICT_STALE 2u    // ratio viejo (cerebro no actualiza)
#define IVANNA_VERDICT_NO_CLOCK 3u // músculo no mide (sin reloj)
#define IVANNA_VERDICT_OFF 4u      // fusión apagada (kill switch)

// El ratio se considera viejo si el cerebro no lo actualizó en este tiempo.
#define IVANNA_RATIO_MAX_AGE_MS 100u

// Bits de fx_flags (qué súper efectos corren en el músculo).
#define IVANNA_FX_ORBE     (1u << 0)
#define IVANNA_FX_HOLOS    (1u << 1)
#define IVANNA_FX_PHI      (1u << 2)
#define IVANNA_FX_VOLTERRA (1u << 3)
#define IVANNA_FX_SUPREME  (1u << 4)
#define IVANNA_FX_NPE      (1u << 5)
#define IVANNA_FX_REALITY  (1u << 6)
#define IVANNA_FX_EQ       (1u << 7)
#define IVANNA_FX_LIMITER  (1u << 8)

typedef struct IvannaFusionState {
    // ── Seqlock + identidad ──────────────────────────────────────────────
    uint32_t sequence;           // par = estable, impar = escribiendo
                                 // (solo se toca vía __atomic_*, ver abajo)
    uint32_t magic;              // IVANNA_FUSION_MAGIC
    uint32_t version;            // IVANNA_FUSION_VERSION
    uint64_t generation;         // crece en cada escritura del cerebro

    // ── MÚSCULO → CEREBRO (mediciones; solo las escribe el músculo) ───
    int64_t  clock_mmap_ns;      // timestamp mmap del kernel (ns)
    uint32_t clock_mmap_frames;  // posición mmap (frames)
    int64_t  clock_write_ns;     // timestamp write-timing (CLOCK_MONOTONIC, ns)
    uint32_t frames_written;     // frames totales que el ASRC escribió al HAL
    uint32_t stream_rate_hz;     // tasa real del stream (48000, 44100, ...)
    uint32_t stream_format;      // formato PAL (1 = PCM16, ...)
    uint32_t stream_channels;    // canales (2 = estéreo)
    uint32_t muscle_health;      // 1 = músculo vivo y midiendo
    uint64_t muscle_timestamp_ms; // cuándo escribió el músculo (CLOCK_MONOTONIC)
    float    coherence;          // coherencia de la envolvente 1–8 Hz [0,1]
                                 // (la mide el músculo del audio que suena;
                                 // el cerebro la usa para actualizar η, Ec. 4)

    // ── CEREBRO → MÚSCULO (decisiones; solo las escribe el cerebro) ───
    int32_t  session_id;         // sesión REAL de música (0 = ninguna)
    uint64_t session_gen;        // generación de la sesión
    float    ratio;              // ratio del servo (salida/entrada); 1.0 = neutro
    float    drift_ppm;          // deriva estimada (ppm)
    uint32_t clock_source;       // IVANNA_CLOCK_* (la decide el cerebro)
    uint32_t verdict;            // IVANNA_VERDICT_* (lo decide el cerebro)
    uint32_t brain_health;       // 1 = cerebro vivo
    uint64_t ratio_timestamp_ms; // cuándo calculó el cerebro el ratio
    uint64_t servo_updates;      // cuántas veces actualizó el cerebro el ratio

    // ── CEREBRO → MÚSCULO: control de la cadena de súper efectos ──────
    // El cerebro decide qué efectos corren y a qué intensidad. Por defecto
    // TODO 0 (neutro): el músculo solo hace ASRC (y ni eso si no hay ratio).
    // El músculo SOLO corre un efecto si su amount > 0. El orden de la
    // cadena en el músculo es fijo: ASRC → ORBE → HOLOS → Φ → Volterra →
    // 5 Ejes → NPE → Reality → EQ Magistral → limitador.
    uint32_t fx_flags;           // bitmask IVANNA_FX_* (qué efectos encendidos)
    float    fx_orbe_amount;     // ORBE (binaural campo cercano) [0,1]
    float    fx_holos_amount;    // HOLOS (campo unificado) [0,1]
    float    fx_phi_amount;      // Φ (hiper-operador) [0,1]
    float    fx_volterra_amount; // Volterra (pre-distorsión) [0,1]
    float    fx_supreme_amount;  // 5 Ejes Supremos [0,1]
    float    fx_npe_amount;      // NPE (neural) [0,1]
    float    fx_reality_amount;  // Reality Recon [0,1]
    float    fx_eq_amount;       // EQ Magistral (curva élite) [0,1]
    uint32_t fx_limiter_on;      // limitador enlazado (1 = on, 0 = off)
    uint64_t fx_generation;      // crece cuando cambia el control de efectos

    // ── CEREBRO → MÚSCULO: el campo Ψ vivo ──────────────────────────────
    // El cerebro corre el núcleo Ψ (PsiCore) y publica aquí el campo que el
    // músculo aplica a TODO el audio. Las ganancias por banda (L/R, 32 bandas
    // ERB) SON el campo: ILD de la Ec. 2 (con TU radio de cabeza) × Γ(η)
    // (Ec. 4) × caída esférica. Si psi_active == 0, las ganancias son 1.0
    // (el campo no actúa; solo el ASRC, si el veredicto es LOADED).
    float    psi_eta;            // η(t): coherencia neuro-acústica [0,1]
    float    psi_gamma;          // Γ(η): acoplamiento neural del campo
    float    psi_src_r;          // r: distancia de la fuente (m)
    float    psi_src_theta;      // θ: azimut de la fuente (rad)
    float    psi_src_phi;        // φ: elevación de la fuente (rad)
    float    psi_gain_l[32];     // ganancia lineal por banda ERB, oído L
    float    psi_gain_r[32];     // ganancia lineal por banda ERB, oído R
    uint32_t psi_active;         // 1 = el campo Ψ está activo
    uint64_t psi_generation;     // crece cuando cambia el campo
    // El campo como FIR (lo diseña el cerebro, lo aplica el músculo):
    // 64 taps por oído, respuesta en magnitud = las 32 ganancias Ψ.
    // El músculo solo convoluciona (64 mults/muestra). Se actualiza cuando
    // el campo cambia (η se mueve en segundos, no en cada muestra).
    float    psi_fir_l[64];      // FIR del campo, oído L (64 taps)
    float    psi_fir_r[64];      // FIR del campo, oído R (64 taps)
    uint64_t psi_fir_gen;        // generación del FIR (el músculo lo recarga si cambia)

    // ── Contadores (los escribe el músculo) ─────────────────────────────
    uint64_t frames_processed;   // frames que el ASRC procesó (no bypass)
    uint64_t frames_passthrough; // frames en passthrough
} IvannaFusionState;

// ── Seqlock: lectura atómica ─────────────────────────────────────────────
// Devuelve 1 si la lectura fue consistente (copia válida en `out`), 0 si
// hay que reintentar. El lector llama en bucle hasta que devuelva 1.
static inline int ivanna_fusion_read(const IvannaFusionState* s,
                                     IvannaFusionState* out) {
    uint32_t seq0 = __atomic_load_n(&s->sequence, __ATOMIC_ACQUIRE);
    if (seq0 & 1u) return 0;  // el escritor está a mitad
    *out = *s;                // copia (el struct es POD; la copia es segura)
    uint32_t seq1 = __atomic_load_n(&s->sequence, __ATOMIC_ACQUIRE);
    if (seq0 != seq1) return 0;  // cambió durante la copia
    return 1;
}

// ── Seqlock: escritura atómica (solo la hace el dueño de esos campos) ───
// El escritor marca impar, escribe, marca par. `w` es el struct ya armado.
static inline void ivanna_fusion_write(IvannaFusionState* s,
                                       const IvannaFusionState* w) {
    __atomic_fetch_add(&s->sequence, 1u, __ATOMIC_RELEASE); // impar
    // Copia todos los campos menos `sequence` (que es el propio seqlock).
    uint32_t seq = __atomic_load_n(&s->sequence, __ATOMIC_RELAXED);
    (void)seq;
    s->magic               = w->magic;
    s->version             = w->version;
    s->generation          = w->generation;
    s->clock_mmap_ns       = w->clock_mmap_ns;
    s->clock_mmap_frames   = w->clock_mmap_frames;
    s->clock_write_ns      = w->clock_write_ns;
    s->frames_written      = w->frames_written;
    s->stream_rate_hz      = w->stream_rate_hz;
    s->stream_format       = w->stream_format;
    s->stream_channels     = w->stream_channels;
    s->muscle_health       = w->muscle_health;
    s->muscle_timestamp_ms = w->muscle_timestamp_ms;
    s->coherence           = w->coherence;
    s->session_id          = w->session_id;
    s->session_gen         = w->session_gen;
    s->ratio               = w->ratio;
    s->drift_ppm           = w->drift_ppm;
    s->clock_source        = w->clock_source;
    s->verdict             = w->verdict;
    s->brain_health        = w->brain_health;
    s->ratio_timestamp_ms  = w->ratio_timestamp_ms;
    s->servo_updates       = w->servo_updates;
    s->fx_flags            = w->fx_flags;
    s->fx_orbe_amount      = w->fx_orbe_amount;
    s->fx_holos_amount     = w->fx_holos_amount;
    s->fx_phi_amount       = w->fx_phi_amount;
    s->fx_volterra_amount  = w->fx_volterra_amount;
    s->fx_supreme_amount   = w->fx_supreme_amount;
    s->fx_npe_amount       = w->fx_npe_amount;
    s->fx_reality_amount   = w->fx_reality_amount;
    s->fx_eq_amount        = w->fx_eq_amount;
    s->fx_limiter_on       = w->fx_limiter_on;
    s->fx_generation       = w->fx_generation;
    s->psi_eta             = w->psi_eta;
    s->psi_gamma           = w->psi_gamma;
    s->psi_src_r           = w->psi_src_r;
    s->psi_src_theta       = w->psi_src_theta;
    s->psi_src_phi         = w->psi_src_phi;
    for (int i = 0; i < 32; ++i) {
        s->psi_gain_l[i] = w->psi_gain_l[i];
        s->psi_gain_r[i] = w->psi_gain_r[i];
    }
    s->psi_active          = w->psi_active;
    s->psi_generation      = w->psi_generation;
    for (int i = 0; i < 64; ++i) {
        s->psi_fir_l[i] = w->psi_fir_l[i];
        s->psi_fir_r[i] = w->psi_fir_r[i];
    }
    s->psi_fir_gen         = w->psi_fir_gen;
    s->frames_processed    = w->frames_processed;
    s->frames_passthrough  = w->frames_passthrough;
    __atomic_fetch_add(&s->sequence, 1u, __ATOMIC_RELEASE); // par
}

#ifdef __cplusplus
}
#endif

#endif // IVANNA_FUSION_STATE_H
