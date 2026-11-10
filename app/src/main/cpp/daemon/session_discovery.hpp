// session_discovery.hpp — Descubrimiento del sessionId REAL de música por el daemon root.
// © 2026 Luis Uriel Pimentel Pérez — IVANNA OMEGA SUPREME
//
// PROBLEMA (medido 2026-11-09, v2.5.378):
//   - El fix del -38 funcionó: el efecto omega se crea (1 Client, sin -38).
//   - PERO procesa 0 frames: quedó en sesión 0 (global), que en este Moto G85
//     falla en silencio (el efecto se crea pero nunca recibe frames). El propio
//     IvannaGlobalEffectManager.kt advierte que los efectos insert en sesión 0
//     son "deprecated/poco fiable: en algunos dispositivos falla en silencio".
//   - El sondeo por reflexión de la app NO descubre sessionIds reales:
//     getActivePlaybackConfigurations() devuelve copias ANONIMIZADAS (-1/0) sin
//     MODIFY_AUDIO_ROUTING (signature|privileged). Por eso la app no puede
//     acoplarse a la sesión real de YouTube Music (la 297).
// SOLUCIÓN: el daemon corre como root, así que ejecuta `dumpsys
// media.audio_flinger` y parsea la sección "Global session refs:" (formato
// AOSP verificado en fuente, frameworks/av AudioFlinger.cpp):
//     Global session refs:
//      session cnt pid uid name
//          297    1   12345 10123 com.google.android.apps.youtube.music
//   y publica el sessionId de música en el snapshot (ABI v11) + GET_STATUS.
//   La app acopla el efecto omega a ESA sesión en lugar de a la sesión 0.
//
// FORMATO (AOSP, android-latest-release):
//   Moderno (Android 14/15): líneas " %7d %4d %7d %6u %s" (session cnt pid
//   uid package) bajo el encabezado " session cnt pid uid name".
//   Antiguo (algunos ROMs): triples "session pid count" separados por comas
//   en una sola línea ("Global session refs: session pid count"), sin package.
//   El parser acepta ambos; el formato antiguo no trae package, así que esas
//   refs solo sirven como fallback sin atribución (package vacío).
//
// GARANTÍAS:
//   - parseAudioFlingerSessionRefs / selectMusicSessionId son PURAS (sin I/O,
//     sin estado global): unit-testeables en host con salidas reales de dumpsys.
//   - Fail-closed: si dumpsys falla, si no hay sección, o si no hay sesión de
//     música, el resultado es kNone/kError (sessionId 0) y la app sigue con su
//     comportamiento actual (sesión 0 como fallback). Nunca se inventa un ID.
//   - Sin malloc en el hot path de audio: el descubrimiento corre en un hilo
//     propio del daemon cada ~3 s (nunca en el callback de audio).
//   - No depende del package exacto de YouTube Music: cualquier sesión que no
//     sea de la propia app ni del sistema y que huela a música gana; si no hay
//     ninguna de música, gana la sesión no excluida más reciente (mayor ID).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace ivanna::daemon {

// ── Referencia de sesión de audio (una línea de "Global session refs") ──────
struct AudioSessionRef {
    int32_t sessionId = 0;   // > 0 si es real; 0/-1 = inválido
    int32_t cnt = 0;         // referencias (cuántos clientes la usan)
    int32_t pid = 0;
    int32_t uid = 0;
    std::string packageName; // vacío en el formato antiguo (sin package)
};

// ── Parseo puro de `dumpsys media.audio_flinger` ────────────────────────────
// Extrae las refs de la sección "Global session refs:". Devuelve vacío si no
// hay sección, si está vacía o si ninguna línea parsea. Nunca lanza.
inline std::vector<AudioSessionRef> parseAudioFlingerSessionRefs(const std::string& dumpsysText) {
    std::vector<AudioSessionRef> refs;
    const std::string kMarker = "Global session refs:";
    const size_t markerPos = dumpsysText.find(kMarker);
    if (markerPos == std::string::npos) return refs;

    // Texto desde el marcador. El resto de la línea del marcador puede traer
    // el encabezado del formato antiguo ("session pid count").
    std::string rest = dumpsysText.substr(markerPos + kMarker.size());
    // Recorre línea por línea.
    size_t pos = 0;
    bool sawAnyRef = false;
    while (pos <= rest.size()) {
        const size_t nl = rest.find('\n', pos);
        const std::string line = (nl == std::string::npos)
            ? rest.substr(pos) : rest.substr(pos, nl - pos);
        pos = (nl == std::string::npos) ? rest.size() + 1 : nl + 1;

        // Línea en blanco: saltar.
        bool blank = true;
        for (char c : line) if (c != ' ' && c != '\t' && c != '\r') { blank = false; break; }
        if (blank) continue;

        // Encabezado (" session cnt pid uid name" / " session pid count"):
        // contiene "session" y no empieza por dígito. Saltar.
        size_t firstNonSpace = 0;
        while (firstNonSpace < line.size() && (line[firstNonSpace] == ' ' || line[firstNonSpace] == '\t')) ++firstNonSpace;
        if (firstNonSpace < line.size() && (line[firstNonSpace] < '0' || line[firstNonSpace] > '9')) {
            // No es una ref (encabezado u otra sección). Si ya vimos refs, la
            // sección terminó; si no, seguir buscando (por si hay basura antes).
            if (sawAnyRef) break;
            continue;
        }

        // Formato moderno: " session cnt pid uid package" (5 campos).
        {
            int session = 0, cnt = 0, pid = 0;
            unsigned uid = 0;
            char pkg[128] = {};
            const int n = std::sscanf(line.c_str(), "%d %d %d %u %127s",
                                      &session, &cnt, &pid, &uid, pkg);
            if (n == 5 && session >= 0) {  // sesión 0 (systemui) sí se registra; el selector la descarta
                refs.push_back(AudioSessionRef{session, cnt, pid, static_cast<int32_t>(uid), pkg});
                sawAnyRef = true;
                continue;
            }
        }
        // Formato antiguo: triples "session pid count" separados por comas.
        {
            bool anyTriple = false;
            size_t tpos = 0;
            while (tpos < line.size()) {
                const size_t comma = line.find(',', tpos);
                const std::string tok = (comma == std::string::npos)
                    ? line.substr(tpos) : line.substr(tpos, comma - tpos);
                tpos = (comma == std::string::npos) ? line.size() + 1 : comma + 1;
                int session = 0, pid = 0, cnt = 0;
                if (std::sscanf(tok.c_str(), "%d %d %d", &session, &pid, &cnt) == 3 && session >= 0) {
                    refs.push_back(AudioSessionRef{session, cnt, pid, 0, ""});
                    anyTriple = true;
                }
            }
            if (anyTriple) { sawAnyRef = true; continue; }
        }
        // Línea que no es ref ni encabezado ni blanco: fin de la sección.
        if (sawAnyRef) break;
    }
    return refs;
}

// ── ¿Huele a música? (substrings en minúsculas sobre el package) ────────────
inline bool packageLooksLikeMusic(const std::string& pkg) noexcept {
    static const char* kMusicHints[] = {
        "music", "spotify", "tidal", "deezer", "soundcloud", "pandora",
        "applemusic", "amazon.music", "youtube", "sound", "audio", "radio",
        "tunein", "iheart", "plex", "vlc", "audible", "podcast", "player",
        "jiosaavn", "gaana", "wynk", "hungama", "napster", "qobuz",
    };
    std::string lower;
    lower.reserve(pkg.size());
    for (char c : pkg) lower.push_back((c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c);
    for (const char* hint : kMusicHints) {
        if (lower.find(hint) != std::string::npos) return true;
    }
    return false;
}

// Paquetes del sistema que nunca son la app de música (ni la propia IVANNA).
inline bool packageIsExcluded(const std::string& pkg, const std::string& ownPackage) noexcept {
    if (pkg.empty()) return true;  // formato antiguo sin package: no atribuible
    if (pkg == ownPackage) return true;
    if (pkg == "com.android.systemui" || pkg == "android" || pkg == "system" ||
        pkg == "com.android.phone" || pkg == "com.android.shell") return true;
    return false;
}

// ── Selección pura: elige el sessionId de música ────────────────────────────
// Agrupa refs por sesión. Descarta sesiones cuyos refs son TODOS de paquetes
// excluidos (propia app / sistema). Entre las restantes prefiere una sesión
// de música; a igualdad, mayor cnt total; luego mayor sessionId (más reciente).
// Devuelve 0 si no hay ninguna sesión elegible (fail-closed).
inline int32_t selectMusicSessionId(const std::vector<AudioSessionRef>& refs,
                                    const std::string& ownPackage = "com.ivanna.omega") noexcept {
    struct Agg { int32_t sessionId = 0; int32_t cnt = 0; bool hasMusic = false; bool hasEligible = false; };
    std::vector<Agg> aggs;
    for (const auto& r : refs) {
        if (r.sessionId <= 0) continue;
        auto it = std::find_if(aggs.begin(), aggs.end(),
                               [&](const Agg& a) { return a.sessionId == r.sessionId; });
        if (it == aggs.end()) { aggs.push_back(Agg{r.sessionId, 0, false, false}); it = aggs.end() - 1; }
        it->cnt += r.cnt;
        if (!packageIsExcluded(r.packageName, ownPackage)) {
            it->hasEligible = true;
            if (packageLooksLikeMusic(r.packageName)) it->hasMusic = true;
        }
    }
    const Agg* best = nullptr;
    for (const auto& a : aggs) {
        if (!a.hasEligible) continue;
        if (best == nullptr) { best = &a; continue; }
        if (a.hasMusic != best->hasMusic) { if (a.hasMusic) best = &a; continue; }
        if (a.cnt != best->cnt) { if (a.cnt > best->cnt) best = &a; continue; }
        if (a.sessionId > best->sessionId) best = &a;
    }
    return best ? best->sessionId : 0;
}

// ── Descubrimiento impuro (popen a dumpsys) ──────────────────────────────────
enum class SessionDiscoveryStatus { kFound, kNone, kError };
struct SessionDiscoveryOutcome {
    SessionDiscoveryStatus status = SessionDiscoveryStatus::kError;
    int32_t sessionId = 0;  // > 0 solo si status == kFound
};

// Ejecuta `dumpsys media.audio_flinger` (root) y devuelve el sessionId de
// música. kError si dumpsys falla (popen/read): el llamador NO debe borrar
// el valor anterior (un fallo transitorio no es "sin música"). kNone si
// dumpsys funciona pero no hay sesión de música: el llamador SÍ debe
// publicar 0 (la música paró). Lectura acotada (256 KB): la sección de
// sesiones está cerca del inicio del dump.
inline SessionDiscoveryOutcome discoverMusicSession(
        const std::string& ownPackage = "com.ivanna.omega") noexcept {
    SessionDiscoveryOutcome out;
    FILE* pipe = ::popen("/system/bin/dumpsys media.audio_flinger 2>/dev/null", "r");
    if (pipe == nullptr) return out;  // kError
    std::string text;
    text.reserve(65536);
    char buf[8192];
    size_t total = 0;
    constexpr size_t kMaxBytes = 256u * 1024u;
    while (total < kMaxBytes) {
        const size_t want = sizeof(buf) < (kMaxBytes - total) ? sizeof(buf) : (kMaxBytes - total);
        const size_t n = std::fread(buf, 1, want, pipe);
        if (n == 0) break;
        text.append(buf, n);
        total += n;
        if (n < want) break;
    }
    const int rc = ::pclose(pipe);
    if (rc != 0 && text.empty()) return out;  // kError (dumpsys falló sin salida)
    const auto refs = parseAudioFlingerSessionRefs(text);
    const int32_t sid = selectMusicSessionId(refs, ownPackage);
    out.status = (sid > 0) ? SessionDiscoveryStatus::kFound : SessionDiscoveryStatus::kNone;
    out.sessionId = sid;
    return out;
}

} // namespace ivanna::daemon
