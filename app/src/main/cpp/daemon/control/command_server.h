#pragma once
// daemon/control/command_server.h

#include <cstddef>
#include <string>

class CommandServer {
public:
    /**
     * Vincula el socket Unix (abstract si empieza con '@').
     * @return true si bind+listen tuvieron éxito.
     */
    bool start(const std::string& socketName);

    /** Cierra el serverFd. Idempotente. */
    void stop();

    /**
     * Escribe @p len bytes de @p data en la región SHM con seqlock.
     * FIX: método que faltaba — sin él el SHM nunca recibía datos.
     * @return true si OmegaShmManager::write() tuvo éxito.
     */
    bool dispatchFrame(const void* data, size_t len);

    /**
     * Loop bloqueante que acepta conexiones y notifica frame_len+epoch.
     * Correr en un std::thread separado. Sale cuando serverFd se cierra.
     * FIX: método que faltaba — sin él nadie atendía @omega_command_socket.
     */
    void acceptLoop();

private:
    int serverFd = -1;
};
