#include <SocketChannel.h>
#include <netbase.h>
#include <Logging.h>

SocketChannel::SocketChannel(SOCKET socket): socket_(socket)
{
}
int SocketChannel::sendData(const void* buffer, size_t len) const
{
    return send(socket_, buffer, len, MSG_NOSIGNAL | MSG_DONTWAIT);
}
int SocketChannel::receiveData(void* buffer, size_t len) const
{
    return recv(socket_, buffer, len, MSG_DONTWAIT);
}
void SocketChannel::close()
{
    CloseSocket(socket_);
}
SOCKET SocketChannel::getSocket() const
{
    return socket_;
}
bool SocketChannel::isValid() const
{
    return socket_ != INVALID_SOCKET;
}
bool SocketChannel::hasErrors(bool logErrors) const
{
    int nErr = WSAGetLastError();
    if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
    {
        if (logErrors)
            LogPrintf("socket recv error %s\n", NetworkErrorString(nErr));
        return true;
    }
    return false;
}