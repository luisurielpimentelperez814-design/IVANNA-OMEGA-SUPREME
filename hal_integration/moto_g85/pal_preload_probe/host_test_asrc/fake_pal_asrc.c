#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct pal_buffer { uint8_t *buffer; size_t size; size_t offset; };

/* Fake PAL: open asigna handles distintos por llamada, close ok, write
 * devuelve el tamano salvo los caminos de error/corto (igual que el fake
 * de la sonda de atributos). Cuenta bytes/llamadas para verificar que el
 * ASRC reenvia lo que produce (no el input original). */
static int g_nopen = 0;
unsigned long long fake_total_bytes = 0;
unsigned long long fake_write_calls = 0;

int32_t pal_stream_open(void *attr, uint32_t n_dev, void *devs, uint32_t n_mod, void *mods, void *cb, uint64_t cookie, void **handle) {
    (void)attr; (void)n_dev; (void)devs; (void)n_mod; (void)mods; (void)cb; (void)cookie;
    if (!handle) return -22;
    ++g_nopen;
    *handle = (void *)(uintptr_t)(0x1000u * (unsigned)g_nopen);
    return 0;
}
int32_t pal_stream_close(void *stream) {
    (void)stream;
    return 0;
}
ssize_t pal_stream_write(void *stream, struct pal_buffer *b) {
    (void)stream;
    if (b->size == 13) return -5;       /* error path must pass through */
    if (b->size == 100) return 60;      /* short write: ASRC keeps the rest pending */
    fake_total_bytes += b->size;
    fake_write_calls++;
    return (ssize_t)b->size;
}
