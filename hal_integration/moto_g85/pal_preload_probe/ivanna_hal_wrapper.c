/* ivanna_hal_wrapper: lanzador del HAL de audio con LD_PRELOAD.
 *
 * Por diseño, un módulo Magisk NO puede hacer que init lea un .rc al
 * arrancar (init parsea /vendor/etc/init/ antes de que Magisk monte los
 * módulos en post-fs-data; overlay.d del módulo no lo lee nadie). Por eso
 * el HAL se lanza con ESTE binario (montado sobre el HAL real por Magisk
 * system/): pone LD_PRELOAD y hace exec del binario real. Es el patrón del
 * módulo adb_root para adbd. Sin forks, sin hilos, sin I/O pesada.
 * Si algo falla, _exit(1) e init reintenta el servicio.
 * Si la .so no existe (o el kill switch está presente), hace exec SIN
 * LD_PRELOAD: audio normal sin ASRC, nunca se rompe el HAL.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include <stdlib.h>

#define REAL_HAL "/vendor/bin/hw/android.hardware.audio.service_64.real"
#define PRELOAD  "/vendor/lib64/libivanna_asrc_write_stage.so"
#define OFF_PATH "/data/vendor/ivanna_audio/pal_asrc.off"

int main(int argc, char **argv) {
    (void)argc;
    /* Solo precargar si la .so existe y el kill switch NO está presente. */
    if (access(PRELOAD, R_OK) == 0 && access(OFF_PATH, F_OK) != 0) {
        setenv("LD_PRELOAD", PRELOAD, 1);
    }
    execv(REAL_HAL, argv);
    _exit(1); /* exec falló: que init reintente el servicio */
}
