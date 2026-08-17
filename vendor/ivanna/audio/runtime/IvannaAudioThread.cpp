
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>


void IvannaSetRealtimeAudioThread()
{

    struct sched_param param{};

    param.sched_priority = 4;


    pthread_setschedparam(
        pthread_self(),
        SCHED_FIFO,
        &param
    );


    setpriority(
        PRIO_PROCESS,
        0,
        -16
    );

}

