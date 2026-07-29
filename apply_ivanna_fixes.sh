#!/usr/bin/env bash
set -euo pipefail

ROOT="$(pwd)"
echo "==> Aplicando fixes IVANNA-OMEGA en: $ROOT"

# ─────────────────────────────────────────────────────────────
# FIX 1 — Workflow: compilar ivanna_daemon con PT_INTERP correcto
# Reemplaza SOLO el step "Compile ivanna_daemon for arm64"
# ─────────────────────────────────────────────────────────────
python3 - <<'PYEOF'
import re, pathlib
p = pathlib.Path(".github/workflows/build.yml")
s = p.read_text()

new_step = r'''      - name: Compile ivanna_daemon for arm64
        run: |
          set -euo pipefail
          TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"
          CXX="$TOOLCHAIN/bin/aarch64-linux-android34-clang++"
          READELF="$TOOLCHAIN/bin/llvm-readelf"
          STRIP="$TOOLCHAIN/bin/llvm-strip"
          DAEMON_SRC="app/src/main/cpp/daemon/ivanna_daemon.cpp"
          [ ! -f "$DAEMON_SRC" ] && DAEMON_SRC=$(find app/src/main/cpp -name "ivanna_daemon.cpp" | head -1 || true)
          echo "Compilando daemon desde: $DAEMON_SRC"
          test -f "$DAEMON_SRC"

          # FIX real: NDK r26 + -pie + -rtlib=compiler-rt + -lc++_shared en el
          # mismo comando dejaba el ELF sin PT_INTERP (Type != DYN). Se pasa
          # a -static-libstdc++ (no depende de libc++_shared en runtime) y
          # se fuerza PIE dinámico vía -Wl,-pie sobre -fpie.
          "$CXX" -std=c++17 -O3 \
            -fPIC -fpie \
            -fstack-protector-strong \
            -D_FORTIFY_SOURCE=2 \
            "$DAEMON_SRC" \
            -static-libstdc++ \
            -Wl,-pie \
            -Wl,-z,relro,-z,now \
            -Wl,-z,noexecstack \
            -Wl,--build-id=sha1 \
            -llog \
            -o ivanna_daemon

          chmod +x ivanna_daemon

          echo "=== Arquitectura ==="
          "$READELF" -h ivanna_daemon | grep Machine
          "$READELF" -h ivanna_daemon | grep -q "AArch64" || { echo "ERROR: no AArch64"; exit 1; }
          echo "=== Tipo de binario ==="
          "$READELF" -h ivanna_daemon | grep -q "Type:.*DYN" || { echo "ERROR: no PIE"; exit 1; }
          echo "=== Intérprete ELF ==="
          "$READELF" -l ivanna_daemon | grep -qi "interp" || { echo "ERROR: sin INTERP"; exit 1; }
          "$READELF" -l ivanna_daemon | grep -i "interp"

          echo "=== Hardening: daemon ==="
          "$READELF" -h ivanna_daemon | grep "Type:"
          "$READELF" -l ivanna_daemon | grep "GNU_RELRO" || echo "WARN: sin RELRO"
          "$READELF" -W -l ivanna_daemon | grep "GNU_STACK" | grep -vi "RWE" || { echo "ERROR: stack ejecutable"; exit 1; }
          "$READELF" -d ivanna_daemon | grep -q "BIND_NOW" && echo "Full RELRO OK" || echo "WARN: BIND_NOW no detectado"

          echo "=== Dependencias dinámicas ==="
          DEPS=$("$READELF" -d ivanna_daemon | grep NEEDED || true)
          echo "$DEPS"
          echo "$DEPS" | grep -q "libc.so"  || { echo "ERROR: no enlaza libc.so"; exit 1; }
          echo "$DEPS" | grep -q "liblog.so" || echo "WARN: liblog.so no listado"

          "$STRIP" --strip-unneeded ivanna_daemon
          ls -lh ivanna_daemon
          "$READELF" -h ivanna_daemon | grep -q "AArch64" || { echo "ERROR: strip corrompió binario"; exit 1; }

          # libc++_shared.so ya no es NEEDED (linkeamos estático), pero la
          # dejamos en el artefacto por compatibilidad con el job de Magisk.
          LIBCXX_SRC=$(find "$ANDROID_NDK_HOME" -name "libc++_shared.so" | grep "aarch64" | head -1 || true)
          [ -f "$LIBCXX_SRC" ] && cp "$LIBCXX_SRC" libc++_shared.so || echo "WARN: libc++_shared.so no encontrado"
          ls -lh libc++_shared.so 2>/dev/null || true
          echo "daemon listo (PIE dinámico, static libstdc++, RELRO+BIND_NOW)."
'''

pat = re.compile(
    r'      - name: Compile ivanna_daemon for arm64\n        run: \|\n(?:          .*\n)+',
    re.MULTILINE
)
s2, n = pat.subn(new_step, s, count=1)
if n != 1:
    raise SystemExit("No se pudo localizar el step 'Compile ivanna_daemon for arm64'")
p.write_text(s2)
print("build.yml: step de daemon reemplazado")
PYEOF

# ─────────────────────────────────────────────────────────────
# FIX 2 — AudioForegroundService: WakeLock + no morir al cambiar ventana
# ─────────────────────────────────────────────────────────────
cat > app/src/main/java/com/ivanna/omega/audio/AudioForegroundService.kt <<'KTEOF'
package com.ivanna.omega.audio

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import androidx.core.app.NotificationCompat
import com.ivanna.omega.R
import com.ivanna.omega.dsp.DSPBridge
import com.ivanna.omega.spatial.IvannaHeadTracker
import com.ivanna.omega.spatial.IvannaSpatialEngine

/**
 * AudioForegroundService — foreground service resistente a cambio de ventana.
 *
 * FIX v3.7:
 *   - PARTIAL_WAKE_LOCK con renovación periódica (evita muerte en background)
 *   - START_STICKY + rearranque idempotente
 *   - Persistencia forzada de sliders vía ParameterStore al detenerse
 */
class AudioForegroundService : Service() {

    companion object {
        const val CHANNEL_ID = "ivanna_audio_channel"
        const val NOTIFICATION_ID = 1
        private const val WAKELOCK_TAG = "ivanna:audio_fg"
        private const val WAKELOCK_TIMEOUT_MS = 10L * 60L * 1000L
    }

    private var audioPipeline: AudioPipeline? = null
    private var headTracker: IvannaHeadTracker? = null
    private var wakeLock: PowerManager.WakeLock? = null
    private var paramStore: ParameterStore? = null

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        DSPBridge.init(96000)
        paramStore = ParameterStore(applicationContext)
        // Restaurar último estado ANTES de arrancar la pipeline
        AudioStateManager.updateState { paramStore!!.loadParameters() }
        acquireWakeLock()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(NOTIFICATION_ID, createNotification())

        if (audioPipeline == null) {
            audioPipeline = AudioPipeline().apply { start(applicationContext) }
        }
        if (headTracker == null) {
            IvannaSpatialEngine.shared.init()
            val tracker = IvannaHeadTracker(applicationContext)
            tracker.init(); tracker.start()
            IvannaSpatialEngine.setHeadTracker(tracker)
            headTracker = tracker
        }
        // Renovar wakelock cada onStartCommand (redundancia útil)
        acquireWakeLock()
        return START_STICKY
    }

    override fun onDestroy() {
        // Persistir SIEMPRE antes de morir
        runCatching { paramStore?.saveParametersNow(AudioStateManager.audioState.value) }
        audioPipeline?.stop(); audioPipeline = null
        headTracker?.release(); headTracker = null
        releaseWakeLock()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // Usuario cerró la app desde recientes: NO matar el servicio,
        // solo persistir y reprogramar el foreground.
        runCatching { paramStore?.saveParametersNow(AudioStateManager.audioState.value) }
        val restart = Intent(applicationContext, AudioForegroundService::class.java)
        startForegroundService(restart)
        super.onTaskRemoved(rootIntent)
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun acquireWakeLock() {
        if (wakeLock?.isHeld == true) return
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, WAKELOCK_TAG).apply {
            setReferenceCounted(false)
            acquire(WAKELOCK_TIMEOUT_MS)
        }
    }

    private fun releaseWakeLock() {
        runCatching { if (wakeLock?.isHeld == true) wakeLock?.release() }
        wakeLock = null
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "IVANNA Audio Processing",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Canal para procesamiento de audio en tiempo real"
                setSound(null, null); enableVibration(false)
            }
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                .createNotificationChannel(channel)
        }
    }

    private fun createNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("IVANNA OMEGA SUPREME")
            .setContentText("Procesamiento de audio activo")
            .setSmallIcon(R.drawable.ic_launcher_foreground)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }
}
KTEOF

# ─────────────────────────────────────────────────────────────
# FIX 3 — AudioStateManager: auto-persistencia de TODO slider/switch
# Se añade un observador único que guarda con debounce cada cambio.
# ─────────────────────────────────────────────────────────────
python3 - <<'PYEOF'
import pathlib, re
p = pathlib.Path("app/src/main/java/com/ivanna/omega/audio/AudioStateManager.kt")
s = p.read_text()

if "attachPersistence" not in s:
    # Inyectar imports si faltan
    if "import kotlinx.coroutines.CoroutineScope" not in s:
        s = s.replace(
            "import kotlinx.coroutines.flow.StateFlow",
            "import kotlinx.coroutines.flow.StateFlow\n"
            "import kotlinx.coroutines.CoroutineScope\n"
            "import kotlinx.coroutines.Dispatchers\n"
            "import kotlinx.coroutines.SupervisorJob\n"
            "import kotlinx.coroutines.launch\n"
            "import kotlinx.coroutines.flow.drop\n"
            "import kotlinx.coroutines.flow.collect\n"
            "import android.content.Context"
        )

    # Añadir el bloque de persistencia justo antes del cierre del object
    inject = r'''
    // ────────────────────────────────────────────────────────────
    // FIX v3.7 — Persistencia automática de sliders/switches
    // Cualquier cambio en audioState se guarda a disco con debounce.
    // Llamar attachPersistence(ctx) UNA vez desde IVANNAApplication.
    // ────────────────────────────────────────────────────────────
    @Volatile private var persistenceAttached = false
    private val persistScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    fun attachPersistence(context: Context) {
        if (persistenceAttached) return
        persistenceAttached = true
        val store = ParameterStore(context.applicationContext)
        // Cargar estado previo al arranque
        val restored = store.loadParameters()
        _audioState.value = restored
        _audioStateLive.postValue(restored)
        // Guardar en cada cambio (debounced dentro de ParameterStore)
        persistScope.launch {
            _audioState.drop(1).collect { store.saveParametersDebounced(it) }
        }
    }
}
'''
    # Reemplazar el último "}" del object AudioStateManager
    # (asumimos que es el último cierre del archivo)
    s = re.sub(r'\}\s*\Z', inject, s, count=1)
    p.write_text(s)
    print("AudioStateManager.kt: attachPersistence() inyectado")
else:
    print("AudioStateManager.kt: ya tenía attachPersistence()")
PYEOF

# Llamar attachPersistence desde IVANNAApplication (si existe)
APPFILE=$(find app/src/main/java -name "IVANNAApplication.kt" | head -1 || true)
if [ -n "$APPFILE" ] && ! grep -q "attachPersistence" "$APPFILE"; then
  python3 - "$APPFILE" <<'PYEOF'
import sys, pathlib, re
p = pathlib.Path(sys.argv[1])
s = p.read_text()
if "com.ivanna.omega.audio.AudioStateManager" not in s:
    s = re.sub(r'(package [^\n]+\n)',
               r'\1import com.ivanna.omega.audio.AudioStateManager\n', s, count=1)
# Inyectar en onCreate
s = re.sub(r'(override fun onCreate\(\)\s*\{\s*super\.onCreate\(\))',
           r'\1\n        AudioStateManager.attachPersistence(this)', s, count=1)
p.write_text(s)
print("IVANNAApplication.kt: attachPersistence(this) llamado en onCreate")
PYEOF
fi

# ─────────────────────────────────────────────────────────────
# Commit + push a main
# ─────────────────────────────────────────────────────────────
git add -A
git -c user.email="ci@ivanna.local" -c user.name="ivanna-ci" \
    commit -m "fix(v3.7): daemon PIE con PT_INTERP + wakelock FG service + persistencia global de sliders"
git push origin HEAD:main

echo "==> LISTO. Fixes empujados a main."
