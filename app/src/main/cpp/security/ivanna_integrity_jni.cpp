// IVANNA OMEGA SUPREME — Puente JNI del módulo de integridad anti-piratería.
// CONFIDENCIAL (interno). Ver ivanna_integrity.h y docs/SECURITY_INTERNAL.md.
//
// Estas funciones JNI las llama IntegrityGate.kt (paquete
// com.ivanna.omega.security). Son JNIEXPORT para que sobrevivan a
// -fvisibility=hidden. Se compilan en libivanna_omega.so (proceso app).
// libomega_effect.so también enlaza el módulo nativo pero NO estas funciones
// JNI (audioserver no usa JNI); el efecto usa integrity_self_check()
// directamente desde omega_process.
#include <jni.h>

#include "ivanna_antitamper.h"
#include "ivanna_integrity.h"

namespace {

using ivanna::security::CertCheck;

// Convierte un jstring a un buffer hex (minúsculas). Retorna false si la
// cadena no es un hex válido de 64 caracteres.
bool jstring_to_hex(JNIEnv* env, jstring s, char out[65]) {
    if (!env || !s) return false;
    const char* c = env->GetStringUTFChars(s, nullptr);
    if (!c) return false;
    bool ok = true;
    size_t len = 0;
    while (c[len] != '\0' && len < 65) ++len;
    if (len != 64) {
        ok = false;
    } else {
        for (size_t i = 0; i < 64; ++i) {
            const char ch = c[i];
            const bool is_lower_hex =
                (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            if (!is_lower_hex) {
                ok = false;
                break;
            }
            out[i] = ch;
        }
        out[64] = '\0';
    }
    env->ReleaseStringUTFChars(s, c);
    return ok;
}

}  // namespace

extern "C" {

// 0 = OK, 1 = MISMATCH, 2 = NO_PIN (sin pin generado).
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeVerifyCert(JNIEnv* env, jobject,
                                                              jstring certSha256Hex) {
    char hex[65] = {};
    if (!jstring_to_hex(env, certSha256Hex, hex)) {
        return static_cast<jint>(CertCheck::kMismatch);
    }
    return static_cast<jint>(ivanna::security::integrity_verify_cert_sha256(hex));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeSetGate(JNIEnv*, jobject, jboolean ok) {
    ivanna::security::integrity_set_gate(ok == JNI_TRUE);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeIsGateOk(JNIEnv*, jobject) {
    return ivanna::security::integrity_gate_ok() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeSelfCheck(JNIEnv*, jobject) {
    return ivanna::security::integrity_self_check() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeLockdown(JNIEnv*, jobject) {
    ivanna::security::integrity_lockdown();
}

// Fase 2: SHA-256 hex de un archivo (la .so instalada). Retorna null si el
// archivo no se puede abrir o leer. IntegrityGate.kt compara con el hash
// generado en build-time (asset ivanna_so_hashes.txt).
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeSha256File(JNIEnv* env, jobject,
                                                              jstring path) {
    if (!env || !path) return nullptr;
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    if (!cpath) return nullptr;
    char hex[65] = {};
    const bool ok = ivanna::security::integrity_sha256_file(cpath, hex);
    env->ReleaseStringUTFChars(path, cpath);
    if (!ok) return nullptr;
    return env->NewStringUTF(hex);
}

// Fase 2: bitmask de señales anti-tamper (ver ivanna_antitamper.h).
// IntegrityGate.kt lo interpreta: lockdown en release si hay señal de
// lockdown; en debug solo se registra.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_security_IntegrityGate_nativeAntiTamperSignals(JNIEnv*, jobject) {
    return static_cast<jint>(ivanna::security::antitamper_evaluate());
}

}  // extern "C"
