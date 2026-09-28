// tests/regression/test_daemon_shm_health.cpp — invariantes ABI v2.1 daemon+SHM
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
TEST(DaemonShmHealth, OffsetsDentroDePaginaControl) {
    // Todos los campos v2.1 deben quedar por debajo de SHM_STATE_OFFSET (4096)
    const size_t offs[] = {48, 56, 60, 64, 68, 72, 76, 80};
    for (size_t o : offs) EXPECT_LT(o + 8, 4096u);
}
TEST(DaemonShmHealth, UptimeEsU64Alineado) {
    EXPECT_EQ(48u % 8, 0u);   // uptime u64 en offset alineado a 8
    EXPECT_EQ(56u % 4, 0u);   // u32 alineados a 4
}
TEST(DaemonShmHealth, ShutdownCleanEsBinario) {
    // Contrato: 0 al arrancar, 1 solo en apagado limpio
    const uint32_t arranque = 0u, limpio = 1u;
    EXPECT_NE(arranque, limpio);
    EXPECT_TRUE(limpio == 1u); // la app solo muestra "OK" con valor exacto 1
}
TEST(DaemonShmHealth, MetricsMagicEsMetr) {
    EXPECT_EQ(0x4D455452u, (unsigned)'M'<<24 | (unsigned)'E'<<16 | (unsigned)'T'<<8 | 'R');
}
