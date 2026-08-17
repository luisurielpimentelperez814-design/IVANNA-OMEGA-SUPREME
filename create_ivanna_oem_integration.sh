#!/data/data/com.termux/files/usr/bin/bash

set -e

ROOT="vendor/ivanna/audio"

echo "[IVANNA] Creating OEM integration layer"


mkdir -p \
$ROOT/vintf \
$ROOT/init \
$ROOT/sepolicy/vendor \
$ROOT/config



#
# VINTF HAL declaration
#

cat <<'EOT' > $ROOT/vintf/android.hardware.audio.core-service.ivanna.xml

<manifest version="1.0" type="device">

    <hal format="aidl">

        <name>
            android.hardware.audio.core
        </name>


        <version>
            1
        </version>


        <fqname>
            IModule/default
        </fqname>


    </hal>


</manifest>

EOT



#
# init vendor service
#

cat <<'EOT' > $ROOT/init/vendor.ivanna.audio.rc


service vendor.audio-hal-ivanna \
    /vendor/bin/hw/android.hardware.audio.core-service.ivanna
{
    class hal

    user audioserver

    group audio
           camera
           media

    capabilities SYS_NICE

    oneshot
}



service vendor.ivanna.dsp \
    /vendor/bin/ivanna_dsp_service
{

    class hal

    user root

    group audio

    oneshot

}

EOT




#
# service_contexts
#

cat <<'EOT' > $ROOT/sepolicy/vendor/service_contexts


android.hardware.audio.core.IModule/default
    u:object_r:hal_audio_hwservice:s0


vendor.ivanna.dsp
    u:object_r:ivanna_dsp_service:s0


EOT




#
# HAL domain
#

cat <<'EOT' > $ROOT/sepolicy/vendor/hal_ivanna_audio.te


type hal_ivanna_audio,
    domain,
    hal_domain;



type hal_ivanna_audio_exec,
    exec_type,
    vendor_file_type,
    file_type;



init_daemon_domain(
    hal_ivanna_audio
)



hal_server_domain(
    hal_ivanna_audio,
    hal_audio
)



allow hal_ivanna_audio
    audioserver:binder
    { call transfer };



allow hal_ivanna_audio
    audio_device:chr_file
    rw_file_perms;



allow hal_ivanna_audio
    self:process
    { execmem };


EOT





#
# DSP SELinux
#

cat <<'EOT' > $ROOT/sepolicy/vendor/ivanna_dsp.te


type ivanna_dsp_service,
    domain;


type ivanna_dsp_service_exec,
    exec_type,
    vendor_file_type,
    file_type;



init_daemon_domain(
    ivanna_dsp_service
)



allow ivanna_dsp_service
    hal_ivanna_audio:binder
    { call transfer };



allow ivanna_dsp_service
    ashmem_device:chr_file
    rw_file_perms;



allow ivanna_dsp_service
    ion_device:chr_file
    rw_file_perms;



EOT





#
# FastRPC SELinux
#

cat <<'EOT' > $ROOT/sepolicy/vendor/ivanna_fastrpc.te


type ivanna_fastrpc,
    domain;



allow ivanna_fastrpc
    vendor_file:file
    { read open execute };



allow ivanna_fastrpc
    ion_device:chr_file
    rw_file_perms;



allow ivanna_fastrpc
    self:process
    execmem;



EOT





#
# file contexts
#

cat <<'EOT' > $ROOT/sepolicy/vendor/file_contexts


/vendor/bin/hw/android.hardware.audio.core-service.ivanna
    u:object_r:hal_ivanna_audio_exec:s0


/vendor/bin/ivanna_dsp_service
    u:object_r:ivanna_dsp_service_exec:s0



/vendor/lib(64)?/hw/audio\.primary\.ivanna\.so
    u:object_r:vendor_file:s0


EOT





#
# Android.bp sepolicy
#

cat <<'EOT' > $ROOT/sepolicy/Android.bp


se_policy_cil {

    name:
    "ivanna_audio_sepolicy",

    srcs:
    [
        "vendor/*.te"
    ]

}


EOT



echo "IVANNA OEM AUDIO INTEGRATION CREATED"

