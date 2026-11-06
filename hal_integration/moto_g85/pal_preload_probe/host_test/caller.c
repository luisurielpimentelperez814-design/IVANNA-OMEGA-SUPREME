#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
struct pal_buffer { uint8_t *buffer; size_t size; size_t offset; };
ssize_t pal_stream_write(void *, struct pal_buffer *);
int main(void) {
    uint8_t pcm[1536]; for (int i = 0; i < 1536; i++) pcm[i] = (uint8_t)(i * 7);
    uint8_t ref[1536]; memcpy(ref, pcm, sizeof pcm);
    int a, b; int bad = 0;
    struct pal_buffer p = { pcm, 768, 0 };
    for (int i = 0; i < 1000; i++) if (pal_stream_write(&a, &p) != 768) bad++;
    p.size = 1536;
    for (int i = 0; i < 500; i++) if (pal_stream_write(&b, &p) != 1536) bad++;
    p.size = 13;  if (pal_stream_write(&a, &p) != -5) bad++;
    p.size = 100; if (pal_stream_write(&a, &p) != 60) bad++;
    if (memcmp(pcm, ref, sizeof pcm)) bad++;
    if (p.buffer != pcm || p.offset != 0) bad++;
    sleep(2);   /* let the dump thread run (period 1s in the test build) */
    printf("caller bad=%d\n", bad);
    return bad;
}
