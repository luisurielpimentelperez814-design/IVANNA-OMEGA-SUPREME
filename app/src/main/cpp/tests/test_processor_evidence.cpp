// Evidencia MEDIDA por procesador (HRTF, RIR, SAF) — sin indicadores de UI.
// Cada prueba tiene un control negativo: si el procesador no hiciera nada,
// o si la dependencia SAF estuviera rota, la prueba falla.
#include <gtest/gtest.h>
#include "HrtfManager.hpp"
#include "spatial/RirConvolver.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"
#include <cmath>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace Ivanna;

namespace {
std::vector<float> noise(size_t n, unsigned seed) {
    std::mt19937 g(seed);
    std::normal_distribution<float> d(0.f, 0.2f);
    std::vector<float> v(n);
    for (auto& x : v) x = d(g);
    return v;
}
// Ejecuta nBlocks de BLOCK_SIZE por HRTF con la misma entrada; devuelve salida L||R concatenada.
std::vector<float> runHrtf(HrtfManager& h, const std::vector<float>& inL, const std::vector<float>& inR) {
    std::vector<float> out;
    for (size_t b = 0; b + BLOCK_SIZE <= inL.size(); b += BLOCK_SIZE) {
        AudioBuffer buf;
        std::memcpy(buf.left, &inL[b], BLOCK_SIZE * 4);
        std::memcpy(buf.right, &inR[b], BLOCK_SIZE * 4);
        h.processBinauralScene(&buf);
        out.insert(out.end(), buf.left, buf.left + BLOCK_SIZE);
        out.insert(out.end(), buf.right, buf.right + BLOCK_SIZE);
    }
    return out;
}
double diffRms(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
    double s = 0; size_t n = std::min(a.size(), b.size());
    for (size_t i = from; i < n; ++i) { double d = a[i] - b[i]; s += d * d; }
    return std::sqrt(s / std::max<size_t>(1, n - from));
}
std::unique_ptr<HrtfManager> mkHrtf() {
    auto h = std::make_unique<HrtfManager>();
    h->setHeadPose(0.f, 0.f, 0.f);
    h->setWetDry(1.f);
    return h;
}
}  // namespace

// ── HRTF ────────────────────────────────────────────────────────────────────
TEST(ProcessorEvidence, HrtfWet1ChangesSignalWet0IsBitExact) {
    const auto in = noise(BLOCK_SIZE * 64, 1), inR = noise(BLOCK_SIZE * 64, 2);
    auto h0 = mkHrtf(); h0->setWetDry(0.f);
    const auto o0 = runHrtf(*h0, in, inR);
    // bypass: L||R por bloque identico a la entrada
    for (size_t b = 0; b < in.size() / BLOCK_SIZE; ++b)
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            ASSERT_EQ(o0[b * 2 * BLOCK_SIZE + i], in[b * BLOCK_SIZE + i]);
            ASSERT_EQ(o0[b * 2 * BLOCK_SIZE + BLOCK_SIZE + i], inR[b * BLOCK_SIZE + i]);
        }
    auto h1 = mkHrtf();
    const auto o1 = runHrtf(*h1, in, inR);
    EXPECT_GT(diffRms(o0, o1, BLOCK_SIZE * 8), 0.01) << "HRTF activa no altera la senal";
    const auto ev = h1->evidence();
    EXPECT_GE(ev.cpuBlocks, 60u);
    EXPECT_GT(ev.outEnergy, 0.0);
}

TEST(ProcessorEvidence, HrtfDependsOnHeadPose) {
    const auto in = noise(BLOCK_SIZE * 64, 3), inR = noise(BLOCK_SIZE * 64, 4);
    auto a = mkHrtf(), b = mkHrtf();
    b->setHeadPose(1.0f, 0.f, 0.f);   // ~57 grados de yaw
    const auto oa = runHrtf(*a, in, inR), ob = runHrtf(*b, in, inR);
    EXPECT_GT(diffRms(oa, ob, BLOCK_SIZE * 16), 0.005) << "la pose no cambia la salida";
}

// ── SAF → HRTF ──────────────────────────────────────────────────────────────
TEST(ProcessorEvidence, SafQReachesHrtfWithoutNewHeadPose) {
    // El refresh espera a que termine el crossfade vigente (XFADE_FRAMES=128 bloques) y luego
    // hace otro de 128: 480 bloques cubren ambos.
    const auto in = noise(BLOCK_SIZE * 480, 5), inR = noise(BLOCK_SIZE * 480, 6);
    const float q0[7] = {0, 0, 0, 0, 0, 0, 0};
    const float q1[7] = {1.f, 1.f, 0, 0, 0, 0, 0};   // curvatura max + sesgo +10 grados
    auto ref = mkHrtf(); ref->setSafLatentQ(q0, 7);
    auto sut = mkHrtf(); sut->setSafLatentQ(q0, 7);
    // Cambia SAF despues de la pose y NO vuelve a llamar setHeadPose
    sut->setSafLatentQ(q1, 7);
    const auto oref = runHrtf(*ref, in, inR), osut = runHrtf(*sut, in, inR);
    EXPECT_GE(sut->evidence().safRefreshes, 1u) << "no hubo re-sintesis por SAF";
    EXPECT_GT(diffRms(oref, osut, BLOCK_SIZE * 2 * 400), 0.002) << "SAF q no altera la salida HRTF";
    EXPECT_NEAR(sut->evidence().safBiasDeg, 10.f, 1e-3f);
    // Control negativo: mismo q => sin refresh adicional ni diferencia
    auto same = mkHrtf(); same->setSafLatentQ(q0, 7); same->setSafLatentQ(q0, 7);
    const auto osame = runHrtf(*same, in, inR);
    EXPECT_LT(diffRms(oref, osame, 0), 1e-6);
}

TEST(ProcessorEvidence, SafInvalidLatentIsRejected) {
    auto h = mkHrtf();
    const float bad[7] = {NAN, 0, 0, 0, 0, 0, 0};
    const float before = h->evidence().curvature;
    h->setSafLatentQ(bad, 7);
    EXPECT_EQ(h->evidence().curvature, before);
}

// ── RIR ─────────────────────────────────────────────────────────────────────
namespace {
void makeIr(std::vector<float>& l, std::vector<float>& r, int n, float rt60 = 0.34f, int sr = 48000) {
    std::mt19937 g(7); std::normal_distribution<float> d(0.f, 1.f);
    l.resize(n); r.resize(n);
    for (int i = 0; i < n; ++i) {
        const float env = std::pow(10.f, -3.f * (i / (float)sr) / rt60);
        l[i] = d(g) * env; r[i] = d(g) * env;
    }
    l[100] += 1.f; r[100] += 1.f;
}
std::vector<float> runRir(RirConvolver& rc, const std::vector<float>& in, const std::vector<float>& inR, size_t blk = 512) {
    std::vector<float> out;
    for (size_t b = 0; b + blk <= in.size(); b += blk) {
        std::vector<float> L(in.begin() + b, in.begin() + b + blk), R(inR.begin() + b, inR.begin() + b + blk);
        rc.process(L.data(), R.data(), (int)blk);
        out.insert(out.end(), L.begin(), L.end());
        out.insert(out.end(), R.begin(), R.end());
    }
    return out;
}
std::unique_ptr<RirConvolver> mkRir(float wet = 1.f) {
    auto rc = std::make_unique<RirConvolver>();
    std::vector<float> l, r; makeIr(l, r, 12000);
    rc->load(l.data(), r.data(), (int)l.size());
    rc->setWetDry(wet);
    return rc;
}
}  // namespace

TEST(ProcessorEvidence, RirWetChangesSignalWet0IsBitExact) {
    const auto in = noise(512 * 40, 11), inR = noise(512 * 40, 12);
    auto off = mkRir(0.f);
    const auto o0 = runRir(*off, in, inR);
    for (size_t b = 0; b < 40; ++b)
        for (size_t i = 0; i < 512; ++i) {
            ASSERT_EQ(o0[b * 1024 + i], in[b * 512 + i]);
            ASSERT_EQ(o0[b * 1024 + 512 + i], inR[b * 512 + i]);
        }
    auto on = mkRir(1.f);
    const auto o1 = runRir(*on, in, inR);
    EXPECT_GT(diffRms(o0, o1, 1024 * 4), 0.02);
    const auto ev = on->evidence();
    EXPECT_GE(ev.activeBlocks, 30u);
    EXPECT_GT(ev.loads, 0u);
}

TEST(ProcessorEvidence, SafWetScaleIsApplied) {
    const auto in = noise(512 * 40, 13), inR = noise(512 * 40, 14);
    // q con wetScale distinto de 1 (si el q elegido no lo mueve, el test falla visiblemente)
    const float qLo[7] = {-1, -1, -1, -1, -1, -1, -1}, qHi[7] = {1, 1, 1, 1, 1, 1, 1};
    float e, d, t, wLo = 1, wHi = 1;
    ivanna::master::computeSofaRirCoupling(qLo, e, d, t, wLo);
    ivanna::master::computeSofaRirCoupling(qHi, e, d, t, wHi);
    ASSERT_NE(wLo, wHi) << "computeSofaRirCoupling no depende de q en wetScale";
    auto a = mkRir(), b = mkRir();
    a->applySofaCoupling(qLo); b->applySofaCoupling(qHi);
    EXPECT_NEAR(a->wetScale(), wLo, 1e-6f);
    EXPECT_NEAR(b->wetScale(), wHi, 1e-6f);
    const auto oa = runRir(*a, in, inR), ob = runRir(*b, in, inR);
    EXPECT_GT(diffRms(oa, ob, 1024 * 4), 1e-3) << "wetScale calculado pero no aplicado";
}

TEST(ProcessorEvidence, SafRecouplesLiveFromRawIr) {
    const auto in = noise(512 * 60, 15), inR = noise(512 * 60, 16);
    const float qA[7] = {-1, -1, -1, -1, -1, -1, -1}, qB[7] = {1, 1, 1, 1, 1, 1, 1};
    auto rc = mkRir();
    rc->applySofaCoupling(qA);
    ASSERT_TRUE(rc->relinkFromRaw());
    ASSERT_FALSE(rc->couplingStale());
    const auto before = rc->evidence();
    rc->applySofaCoupling(qB);
    EXPECT_TRUE(rc->couplingStale()) << "q nuevo no marca la IR horneada como desfasada";
    ASSERT_TRUE(rc->relinkFromRaw());
    EXPECT_FALSE(rc->couplingStale());
    const auto after = rc->evidence();
    EXPECT_EQ(after.relinks, before.relinks + 1);
    EXPECT_NE(after.bakedDecorrel, before.bakedDecorrel);
    // Efecto audible medido: IR re-acoplada produce otra salida que la sin re-acoplar
    auto stale = mkRir();
    stale->applySofaCoupling(qA); stale->relinkFromRaw();
    stale->applySofaCoupling(qB);                     // solo atomics: la IR sigue horneada con qA
    auto fresh = mkRir();
    fresh->applySofaCoupling(qA); fresh->relinkFromRaw();
    fresh->applySofaCoupling(qB); fresh->relinkFromRaw();
    stale->setWetDry(1.f); fresh->setWetDry(1.f);
    // igualar wetScale para aislar el efecto de la IR
    const auto os = runRir(*stale, in, inR), of = runRir(*fresh, in, inR);
    EXPECT_GT(diffRms(os, of, 1024 * 6), 1e-4) << "re-acoplamiento no altera la convolucion";
}

TEST(ProcessorEvidence, Rt60DeltaChangesTailDecay) {
    std::vector<float> l, r; makeIr(l, r, 14000);
    const float qs[2][7] = {{-1, -1, -1, -1, -1, -1, -1}, {1, 1, 1, 1, 1, 1, 1}};
    float e, d, t0 = 0, t1 = 0, w;
    ivanna::master::computeSofaRirCoupling(qs[0], e, d, t0, w);
    ivanna::master::computeSofaRirCoupling(qs[1], e, d, t1, w);
    ASSERT_NE(t0, t1) << "rt60Delta no depende de q";
    auto tail = [&](const float* q) {
        RirConvolver rc; rc.applySofaCoupling(q);
        rc.load(l.data(), r.data(), (int)l.size()); rc.setWetDry(1.f);
        std::vector<float> imp(512 * 28, 0.f), z(512 * 28, 0.f); imp[0] = 1.f;
        const auto o = runRir(rc, imp, z);
        // energia L en el ultimo tercio de salida (cola)
        double s = 0; const size_t n = 512 * 28;
        for (size_t b = 18; b < 28; ++b) for (size_t i = 0; i < 512; ++i) s += (double)o[b * 1024 + i] * o[b * 1024 + i];
        (void)n; return s;
    };
    const double e0 = tail(qs[0]), e1 = tail(qs[1]);
    EXPECT_NE(e0, e1);
    // mayor rt60Delta => cola mas larga => mas energia tardia
    if (t1 > t0) EXPECT_GT(e1, e0); else EXPECT_GT(e0, e1);
}
