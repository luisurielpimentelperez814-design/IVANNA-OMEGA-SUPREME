// ============================================================================
// hexagon_dsp_integration.cpp — implementación real (delegación al loader)
// © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
// ============================================================================

#include "hexagon_dsp_integration.hpp"

namespace ivanna { namespace hexagon {

// Puentes declarados en ivanna_dsp.cpp
namespace rt {
    bool        ensure_loaded()   noexcept;
    bool        is_available()    noexcept;
    const char* active_library()  noexcept;
    void        release()         noexcept;
}

bool ensure_available() noexcept { return rt::ensure_loaded(); }
bool is_available()     noexcept { return rt::is_available(); }
const char* active_library() noexcept { return rt::active_library(); }
void release() noexcept { rt::release(); }

}} // namespace ivanna::hexagon
