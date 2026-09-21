package com.ivanna.omega.spatial
import android.content.Context
import android.util.Log

/**
 * WfsCalibrationManager — puente UI→JNI→daemon para la calibracion geometrica del WFS.
 * Persiste la posicion del oyente y el preset de altavoces; reencoda los 7 canales
 * FL,FR,SL,SR,TL,TR,SW al snapshot (wfs_speaker_x/y/z) que el daemon publica y
 * omega_effect entrega a WfsRenderer.setSpeakerLayout3D() cada bloque. Cero cambio ABI:
 * reusa los campos de geometria ya persistidos en el OmegaDspSnapshot.
 *
 * Geometria por defecto = RoomGeometryConfig.hpp::defaultLayout (sala 3.5x7.0x3.5 m).
 */
object WfsCalibrationManager {
    private const val TAG = "WfsCalibration"
    private const val PREFS = "ivanna_wfs_calib"
    private const val KEY_LX="lx"; private const val KEY_LY="ly"; private const val KEY_LZ="lz"
    private const val KEY_PRESET="preset"

    // Geometria por defecto espejo de RoomGeometryConfig::defaultLayout (7 altavoces).
    val DEFAULT_X = floatArrayOf(0.35f,3.15f,0.25f,3.25f,0.25f,3.25f,3.00f)
    val DEFAULT_Y = floatArrayOf(1.50f,1.50f,1.50f,1.50f,3.00f,3.00f,0.15f)
    val DEFAULT_Z = floatArrayOf(0.00f,0.00f,3.50f,3.50f,3.50f,3.50f,3.00f)

    @Volatile var listenerX=1.75f; @Volatile var listenerY=1.20f; @Volatile var listenerZ=3.50f
    @Volatile var presetName="7ch-default"

    private fun p(c:Context)=c.getSharedPreferences(PREFS,Context.MODE_PRIVATE)

    /** Aplica un layout de altavoces al motor (JNI -> daemon snapshot). */
    fun applyLayout(context:Context, x:FloatArray=DEFAULT_X, y:FloatArray=DEFAULT_Y, z:FloatArray=DEFAULT_Z, name:String=presetName){
        runCatching{
            com.ivanna.omega.core.IvannaNativeLib.nativeSetWfsSpeakerLayout(x,y,z)
            presetName=name
            p(context).edit().putString(KEY_PRESET,name).apply()
        }.onFailure{ Log.w(TAG,"applyLayout: ${it.message}") }
    }

    /** Fija la posicion del oyente (persistida) y re-publica el layout actual. */
    fun setListener(context:Context, lx:Float, ly:Float, lz:Float){
        listenerX=lx; listenerY=ly; listenerZ=lz
        p(context).edit().putFloat(KEY_LX,lx).putFloat(KEY_LY,ly).putFloat(KEY_LZ,lz).apply()
        // El motor recibe geometria relativa al oyente: desplazar el array completo.
        applyLayout(context,
            DEFAULT_X.map{ it-lx }.toFloatArray(),
            DEFAULT_Y.map{ it-ly }.toFloatArray(),
            DEFAULT_Z.map{ it-lz }.toFloatArray())
    }

    /** Restaura la calibracion persistida al arranque (desde PersistedStateRestorer). */
    fun restore(context:Context){
        runCatching{
            val pr=p(context)
            listenerX=pr.getFloat(KEY_LX,1.75f); listenerY=pr.getFloat(KEY_LY,1.20f); listenerZ=pr.getFloat(KEY_LZ,3.50f)
            presetName=pr.getString(KEY_PRESET,"7ch-default")?:"7ch-default"
            applyLayout(context)
        }.onFailure{ Log.w(TAG,"restore: ${it.message}") }
    }
}
