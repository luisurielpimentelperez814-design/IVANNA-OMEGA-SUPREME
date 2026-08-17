package android.hardware.audio.core.ivanna;

interface IAudioHw {

    void init();

    void startStream();

    void stopStream();

    float getDspLoad();

}
