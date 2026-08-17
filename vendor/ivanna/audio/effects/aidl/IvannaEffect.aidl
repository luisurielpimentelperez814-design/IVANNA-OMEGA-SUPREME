
package android.hardware.audio.effect.ivanna;


interface IvannaEffect {


    boolean init();


    void setParameter(
        int id,
        int value
    );


    void process(
        in byte[] input,
        out byte[] output
    );


    void release();


}

