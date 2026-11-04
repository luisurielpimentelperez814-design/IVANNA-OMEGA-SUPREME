#define _GNU_SOURCE
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Presence-only LD_PRELOAD diagnostic. It defines NO pal_* symbols, does not
 * interpose calls, inspect PCM or change audio behavior. One constructor-time
 * snapshot records process identity, matching loaded objects and PAL symbol
 * visibility. It performs no work on audio callbacks.
 */
#define TAG "IVANNA_PAL_PROBE"
#define OUT_FILE "/data/vendor/ivanna_audio/pal_preload_presence.txt"

static void out(int fd, const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (n >= (int)sizeof(b)) n = (int)sizeof(b) - 1;
        (void)write(fd, b, (size_t)n);
    }
}
struct scan_ctx { int fd; unsigned count; };
static int scan_object(struct dl_phdr_info *i, size_t z, void *p) {
    (void)z;
    struct scan_ctx *c = (struct scan_ctx *)p;
    const char *n = i->dlpi_name ? i->dlpi_name : "";
    if (!strstr(n,"audio") && !strstr(n,"pal") && !strstr(n,"agm") &&
        !strstr(n,"blair") && !strstr(n,"dolby")) return 0;
    out(c->fd, "OBJECT base=0x%lx name=%s\n",
        (unsigned long)i->dlpi_addr, n[0] ? n : "<main>");
    c->count++;
    return 0;
}
__attribute__((constructor))
static void ivanna_pal_probe_init(void) {
    char proc[256] = {0};
    int p = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (p >= 0) {
        ssize_t n = read(p, proc, sizeof(proc)-1);
        if (n > 0) for (ssize_t i=0; i<n; ++i) if (!proc[i]) proc[i]=' ';
        close(p);
    }
    int fd = open(OUT_FILE, O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC, 0660);
    if (fd < 0) {
        __android_log_print(ANDROID_LOG_ERROR,TAG,
            "loaded pid=%d but cannot write report errno=%d",getpid(),errno);
        return;
    }
    out(fd,"\n=== IVANNA PAL PRELOAD PRESENCE v1 ===\n");
    out(fd,"pid=%d uid=%d process=%s\n",getpid(),getuid(),proc[0]?proc:"<unknown>");
    struct scan_ctx c={.fd=fd,.count=0};
    dl_iterate_phdr(scan_object,&c);
    out(fd,"matching_objects=%u\n",c.count);
    const char *names[]={"pal_stream_write","pal_get_timestamp",
        "pal_stream_get_mmap_position","pal_stream_open","pal_stream_start",NULL};
    for (unsigned i=0; names[i]; ++i) {
        dlerror();
        void *s=dlsym(RTLD_DEFAULT,names[i]);
        const char *e=dlerror();
        out(fd,"SYMBOL %s=%s",names[i],s?"visible":"not_visible");
        if (s) out(fd," address=0x%lx",(unsigned long)s);
        if (e) out(fd," error=%s",e);
        out(fd,"\n");
    }
    out(fd,"mode=presence-only; no PAL interposition; no PCM access\n=== END ===\n");
    close(fd);
    __android_log_print(ANDROID_LOG_INFO,TAG,
        "presence snapshot written pid=%d objects=%u",getpid(),c.count);
}
