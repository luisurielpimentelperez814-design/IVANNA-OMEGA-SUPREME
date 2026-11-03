package com.ivanna.omega.magisk

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.system.Os
import android.system.OsConstants

/** Fail immediately if the abstract socket cannot accept a connection, including a full backlog.
 * The caller owns and closes the socket on both success and failure.
 */
internal fun connectAbstractNonBlocking(socket: LocalSocket, name: String) {
    // getInputStream creates the descriptor without connecting or binding it.
    socket.inputStream
    val fd = socket.fileDescriptor
    val flags = Os.fcntlInt(fd, OsConstants.F_GETFL, 0)
    Os.fcntlInt(fd, OsConstants.F_SETFL, flags or OsConstants.O_NONBLOCK)
    socket.connect(LocalSocketAddress(name, LocalSocketAddress.Namespace.ABSTRACT))
    // Read/write timeouts require blocking mode. If restoring flags fails, the caller closes it.
    Os.fcntlInt(fd, OsConstants.F_SETFL, flags)
}
