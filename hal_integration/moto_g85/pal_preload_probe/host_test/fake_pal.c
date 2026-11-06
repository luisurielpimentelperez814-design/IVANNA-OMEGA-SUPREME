#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
struct pal_buffer { uint8_t *buffer; size_t size; size_t offset; };
ssize_t pal_stream_write(void *stream, struct pal_buffer *b) {
    (void)stream;
    if (b->size == 13) return -5;       /* error path must pass through */
    if (b->size == 100) return 60;      /* short write must pass through */
    return (ssize_t)b->size;
}
