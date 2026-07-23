/**
 * ivanna_daemon.cpp — IVANNA OMEGA SUPREME system daemon
 *
 * Root daemon deployed by Magisk.
 * Provides OmegaSharedState IPC shared memory.
 *
 * Target:
 *   arm64-v8a PIE executable
 *
 * Deploy:
 *   /system/bin/ivanna_daemon
 */

#include <android/log.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <atomic>
#include <cstring>
#include <cerrno>
#include <cstdint>

#define LOG_TAG "ivanna_daemon"

#define LOGI(...) __android_log_print(
    ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define LOGE(...) __android_log_print(
    ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define LOGD(...) __android_log_print(
    ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)


static constexpr const char* SHM_NAME = "/omega_shm";
static constexpr size_t SHM_SIZE = 4096;
static constexpr uint32_t DAEMON_MAGIC = 0x4F4D4741U;


/*
 * Shared protocol.
 * Must match libomega_effect.so
 */

struct OmegaSharedState {

    uint32_t magic;
    uint32_t version;


    /*
     * Runtime parameters
     */

    float ai_runtime_gain_mul;
    float target_gain;

    float compressor_amount;
    float exciter_reduction;
    float spatial_width;


    /*
     * Seqlock counter
     *
     * odd   = writer active
     * even  = stable state
     */

    uint32_t seq;


    uint8_t _pad[
        SHM_SIZE
        - (5 * sizeof(float))
        - (3 * sizeof(uint32_t))
    ];
};


static_assert(
    sizeof(OmegaSharedState) == SHM_SIZE,
    "OmegaSharedState size mismatch"
);



static std::atomic<bool> g_running(true);



static void sig_handler(int)
{
    g_running.store(false);
}



static OmegaSharedState* open_shm()
{

    int fd = shm_open(
        SHM_NAME,
        O_CREAT | O_RDWR,
        0660
    );


    if(fd < 0)
    {
        LOGE(
            "shm_open failed: %s",
            strerror(errno)
        );

        return nullptr;
    }



    if(ftruncate(fd, SHM_SIZE) < 0)
    {
        LOGE(
            "ftruncate failed: %s",
            strerror(errno)
        );

        close(fd);
        return nullptr;
    }



    void* ptr = mmap(
        nullptr,
        SHM_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );


    close(fd);



    if(ptr == MAP_FAILED)
    {
        LOGE(
            "mmap failed: %s",
            strerror(errno)
        );

        return nullptr;
    }


    return reinterpret_cast<OmegaSharedState*>(ptr);
}




static void init_shared_state(
    OmegaSharedState* shm
)
{

    memset(
        shm,
        0,
        SHM_SIZE
    );


    shm->magic = DAEMON_MAGIC;

    shm->version = 2;


    /*
     * CRITICAL BOOT GUARD
     *
     * Prevents silent output bug
     */

    shm->ai_runtime_gain_mul = 1.0f;

    shm->target_gain = 1.0f;

    shm->compressor_amount = 0.0f;

    shm->exciter_reduction = 0.0f;

    shm->spatial_width = 0.5f;


    shm->seq = 0;


    __sync_synchronize();

}





int main()
{

    LOGI(
        "IVANNA OMEGA SUPREME daemon start pid=%d",
        getpid()
    );


    signal(SIGTERM, sig_handler);

    signal(SIGINT, sig_handler);



    OmegaSharedState* shm = open_shm();



    if(!shm)
    {

        LOGE(
            "Shared memory init failed"
        );

        return 1;
    }



    init_shared_state(shm);



    LOGI(
        "SHM ready %s size=%zu gain=%.2f",
        SHM_NAME,
        SHM_SIZE,
        shm->ai_runtime_gain_mul
    );





    while(
        g_running.load()
    )
    {


        /*
         * Heartbeat
         */

        sleep(30);



        if(g_running.load())
        {

            uint32_t seq1;
            uint32_t seq2;


            float gain;
            float comp;
            float exc;
            float spatial;



            do {

                seq1 = shm->seq;


                if(seq1 & 1)
                    continue;


                __sync_synchronize();


                gain = shm->ai_runtime_gain_mul;

                comp = shm->compressor_amount;

                exc = shm->exciter_reduction;

                spatial = shm->spatial_width;


                __sync_synchronize();


                seq2 = shm->seq;


            } while(
                seq1 != seq2
                ||
                (seq1 & 1)
            );



            LOGD(
                "heartbeat gain=%.3f comp=%.3f exc=%.3f spatial=%.3f",
                gain,
                comp,
                exc,
                spatial
            );
        }
    }




    LOGI(
        "IVANNA daemon stopping"
    );


    munmap(
        shm,
        SHM_SIZE
    );


    /*
     * Do NOT shm_unlink().
     *
     * Other processes may still hold
     * the shared memory descriptor.
     */


    return 0;
}

