#include "include/SaFStimulusRenderer.hpp"

#include <cmath>
#include <algorithm>
#include <random>

namespace ivanna {

SaFStimulusRenderer::SaFStimulusRenderer()
    :
    m_sampleRate(48000),
    m_blockSize(512),
    m_azimuth(0.0f),
    m_elevation(0.0f),
    m_ready(false)
{
}


bool SaFStimulusRenderer::initialize(
        uint32_t sampleRate,
        uint32_t blockSize)
{
    m_sampleRate = sampleRate;
    m_blockSize = blockSize;

    m_convolver.init(sampleRate);

    m_ready = true;

    return true;
}


void SaFStimulusRenderer::setDirection(
        float azimuth,
        float elevation)
{
    m_azimuth = azimuth;
    m_elevation = elevation;

    if(!m_ready)
        initialize(48000,512);

    // FIX (regresion documentada en AGENT_CLAIMS.md, 2026-09-11): antes la
    // elevacion se guardaba pero NUNCA se leia y el convolver recibia un
    // valor constante 0.75f — ARRIBA sonaba identico a ENFRENTE. Ahora la
    // agresividad del convolver refleja la elevacion pedida: a mayor
    // |elevacion|, mas enfatizadas las pistas espectrales (6-10kHz) que
    // el oido usa para distinguir ARRIBA/ATRAS de ENFRENTE (modelo de
    // Woodworth/Blauert, el mismo dominio que la version Kotlin ya
    // corregida en SaFStimulusPlayer.kt — paridad Kotlin↔nativo).
    constexpr float kBaseAggressiveness = 0.75f;
    const float elNorm = std::min(std::fabs(m_elevation) / 90.0f, 1.0f);
    const float aggressiveness =
        std::min(kBaseAggressiveness + 0.5f * elNorm, 1.0f);

    m_convolver.set_position(
        azimuth,
        aggressiveness
    );
}


void SaFStimulusRenderer::createBiologicalStimulus(
        std::vector<float>& mono,
        float seconds)
{
    uint32_t samples =
        static_cast<uint32_t>(
            m_sampleRate * seconds
        );

    mono.resize(samples);


    // FIX (misma regresion documentada): antes era un tono puro de 880Hz.
    // Un tono puro NO lleva las pistas espectrales de 6-10kHz que las
    // HRTF modulan para distinguir direcciones verticales/frontales —
    // con el, todas las direcciones frontales/elevadas del test SaF
    // eran indistinguibles entre si. Se reemplaza por un tren de clics
    // de banda ancha (ruido rosa suavizado con envolvente), el estimulo
    // estandar en calibracion HRTF (sintetizado, sin assets externos):
    // energia en TODA la banda audible, incluida 6-10kHz, para que la
    // HRTF sintetica del convolver pueda imprimir las pistas
    // direccionales reales.
    std::mt19937 rng(0x1A55A5u);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);

    // One-pole lowpass sobre ruido blanco -> ruido rosa aproximado
    // (evita el hiss brillante puro, conserva la banda alta suficiente).
    float lp = 0.0f;
    const float lpCoef = 0.72f;

    // 3 rafagas cortas separadas: mas facil de localizar que un sonido
    // continuo y audiblemente distinto a cualquier tono de sistema.
    const uint32_t burstLen =
        std::min(static_cast<uint32_t>(m_sampleRate * 0.06f), samples);
    const uint32_t gap =
        samples > 3u * burstLen ? (samples - 3u * burstLen) / 3u : 0u;

    for(uint32_t i = 0; i < samples; i++)
    {
        const uint32_t phase = i % (burstLen + gap);
        const bool inBurst = (gap == 0u) || (phase < burstLen);

        float s = 0.0f;
        if(inBurst)
        {
            lp += lpCoef * (noise(rng) - lp);
            const float raw = lp * 3.0f; // recuperar nivel del one-pole

            // Envolvente senoidal de 5ms de ataque + caida suave dentro
            // de la rafaga (evita clics duros de discontinuidad).
            const uint32_t bi = (gap == 0u) ? i : phase;
            const float t =
                static_cast<float>(bi) / static_cast<float>(m_sampleRate);
            const float attack =
                std::min(t / 0.005f, 1.0f);
            const float tail =
                std::min(static_cast<float>(burstLen - bi) /
                         static_cast<float>(burstLen) * 4.0f, 1.0f);
            s = raw * attack * tail * 0.25f;
        }

        mono[i] = s;
    }
}



bool SaFStimulusRenderer::generateCalibrationStimulus(
        std::vector<float>& left,
        std::vector<float>& right,
        float durationSeconds)
{
    if(!m_ready)
        initialize(48000,512);


    std::vector<float> mono;


    createBiologicalStimulus(
        mono,
        durationSeconds
    );


    left.resize(mono.size());
    right.resize(mono.size());


    // FIX (el nucleo de la regresion documentada): antes solo habia
    // panning por sin(azimut) — con sin(0°)=sin(180°)=0, ENFRENTE y
    // ATRAS daban ganancias L/R IDENTICAS, y m_convolver (la HRTF real)
    // se inicializaba y posicionaba pero su salida NUNCA se usaba.
    // Resultado: de las 5 direcciones del test SaF, solo 2 sonaban
    // distintas. Ahora el mono pasa de verdad por el convolver HRTF,
    // cuya salida SI se escribe en left/right — cada direccion recibe
    // sus pistas ITD/ILD/espectrales reales.
    const uint32_t n = static_cast<uint32_t>(mono.size());
    m_convolver.process(
        mono.data(), mono.data(),
        left.data(), right.data(),
        n
    );


    return true;
}



void SaFStimulusRenderer::reset()
{
    m_azimuth = 0.0f;
    m_elevation = 0.0f;

    m_convolver.reset();

    m_ready = false;
}


}
