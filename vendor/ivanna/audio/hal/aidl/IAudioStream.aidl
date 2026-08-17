package android.hardware.audio.core.ivanna;

interface IAudioStream {

    void writeAudio(in byte[] buffer);

    void setVolume(float volume);

}
