#pragma once
/**
 * hexagon_dsp_integration.hpp
 * Qualcomm Hexagon DSP offload interface — STUB
 *
 * Full implementation requires Qualcomm FastRPC SDK (proprietary).
 * See ivanna_fastrpc_client.hpp for the client-side wrapper.
 * Status: pending — no real offload until SDK is available.
 */
#include <cstdint>
namespace ivanna::hexagon {
    bool is_available() noexcept;
    void release()     noexcept;
}
