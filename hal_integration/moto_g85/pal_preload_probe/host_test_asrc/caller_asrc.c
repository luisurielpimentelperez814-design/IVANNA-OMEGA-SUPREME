#define _POSIX_C_SOURCE 200809L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
struct pal_buffer { uint8_t *buffer; size_t size; size_t offset; };
int32_t pal_stream_open(void *attr, uint32_t n_dev, void *devs, uint32_t n_mod, void *mods, void *cb, uint64_t cookie, void **handle);
int32_t pal_stream_close(void *stream);
ssize_t pal_stream_write(void *stream, struct pal_buffer *b);

/* Atributos con los offsets REALES (w14=rate, w15=bits, w16=fmt, w17=ch). */
static void fill_attr_real(uint32_t *w, uint32_t rate, uint32_t bits, uint32_t fmt, uint32_t ch) {
    for (int i = 0; i < 32; ++i) w[i] = 0;
    w[0] = 2;
    w[14] = rate; w[15] = bits; w[16] = fmt; w[17] = ch;
}
/* Stream de datos de musica: w14..w17 en CERO (el caso real del telefono). */
static void fill_attr_zeros(uint32_t *w) {
    for (int i = 0; i < 32; ++i) w[i] = 0;
    w[0] = 1;
}
static ssize_t wr(void *h, const void *pcm, size_t n) {
    struct pal_buffer b = { (uint8_t *)pcm, n, 0 };
    return pal_stream_write(h, &b);
}
int main(void) {
    static int16_t pcm[192 * 2];
    for (int i = 0; i < 192 * 2; ++i) pcm[i] = (int16_t)(i * 100);
    uint32_t attrMusic[32], attrVoice[32], attrZeros[32], attrHiRes[32];
    void *hMusic = NULL, *hVoice = NULL, *hZeros = NULL, *hHiRes = NULL;
    fill_attr_real(attrMusic, 48000, 16, 1, 2);
    fill_attr_real(attrVoice, 16000, 16, 1, 2);
    fill_attr_real(attrHiRes, 192000, 16, 1, 2);
    fill_attr_zeros(attrZeros);

    /* Musica por attr (48k/16/2): elegible, se resamplea. */
    if (pal_stream_open(attrMusic, 1, NULL, 0, NULL, NULL, 0, &hMusic) != 0 || !hMusic) { printf("open music failed\n"); return 1; }
    for (int i = 0; i < 10; ++i)
        if (wr(hMusic, pcm, 768) != 768) { printf("write music failed\n"); return 1; }
    /* Misaligned (770 B): passthrough, devuelve 770. */
    if (wr(hMusic, pcm, 770) != 770) { printf("misaligned write failed\n"); return 1; }
    /* Error (13 B): passthrough (misaligned), devuelve -5 del fake. */
    if (wr(hMusic, pcm, 13) != -5) { printf("error write failed\n"); return 1; }
    /* Short (100 B): el ASRC consume el input (devuelve 100) y guarda el
     * resto pendiente; el fake devuelve 60 pero el caller ve 100. */
    if (wr(hMusic, pcm, 100) != 100) { printf("short write failed\n"); return 1; }
    /* Un write mas drena el pendiente (40 B) y procesa normal. */
    if (wr(hMusic, pcm, 768) != 768) { printf("write after short failed\n"); return 1; }

    /* Voz (16k): formato valido pero no musica -> passthrough. */
    if (pal_stream_open(attrVoice, 1, NULL, 0, NULL, NULL, 0, &hVoice) != 0 || !hVoice) { printf("open voice failed\n"); return 1; }
    for (int i = 0; i < 3; ++i)
        if (wr(hVoice, pcm, 768) != 768) { printf("write voice failed\n"); return 1; }

    /* Datos de musica (w14..w17 = 0): elegible por formato CONOCIDO. */
    if (pal_stream_open(attrZeros, 1, NULL, 0, NULL, NULL, 0, &hZeros) != 0 || !hZeros) { printf("open zeros failed\n"); return 1; }
    for (int i = 0; i < 5; ++i)
        if (wr(hZeros, pcm, 768) != 768) { printf("write zeros failed\n"); return 1; }

    /* Hi-res (192 kHz/16/2): elegible en v5 (correccion ppm a 192 kHz,
     * el FIR escala en frecuencia normalizada, passband 0-80 kHz). */
    if (pal_stream_open(attrHiRes, 1, NULL, 0, NULL, NULL, 0, &hHiRes) != 0 || !hHiRes) { printf("open hires failed\n"); return 1; }
    for (int i = 0; i < 3; ++i)
        if (wr(hHiRes, pcm, 768) != 768) { printf("write hires failed\n"); return 1; }

    /* Cerrar voz: debe desaparecer del dump. Musica y zeros quedan abiertos. */
    if (pal_stream_close(hVoice) != 0) { printf("close voice failed\n"); return 1; }
    /* Escribir a zeros DESPUES de cerrar voz: voz (0x2000) y zeros (0x3000)
     * colisionan con musica (0x1000) en el hash (los tres hashean al slot
     * 0). Con el bug de borrado (handle=0) el close de voz rompia la
     * cadena de sondaje y este write era pt_no_slot; con tombstones debe
     * procesarse normal (la causa del pt_no_slot del telefono). */
    if (wr(hZeros, pcm, 768) != 768) { printf("write zeros after close failed\n"); return 1; }
    /* El hilo de control escribe cada 1 s; dormir 2.5 s garantiza >=2 dumps.
     * Ademas da tiempo a que el probe mmap falle (sin libar-pal.so en host)
     * y la fuente de reloj quede en write-timing. */
    struct timespec ts = { 2, 500000000L };
    nanosleep(&ts, NULL);
    printf("caller done hMusic=%p hVoice=%p hZeros=%p\n", hMusic, hVoice, hZeros);
    return 0;
}
