#include <SocketChannel.h>
#include <netbase.h>
#include <Logging.h>

SocketChannel::SocketChannel(SOCKET socket): socket_(socket)
{
}
int SocketChannel::sendData(const void* buffer, size_t len) const
{
#ifdef WIN32
    return send(socket_, (const char*)buffer, len, MSG_NOSIGNAL | MSG_DONTWAIT);
#else
    return send(socket_, buffer, len, MSG_NOSIGNAL | MSG_DONTWAIT);
#endif
}
int SocketChannel::receiveData(void* buffer, size_t len) const
{
#ifdef WIN32
    return recv(socket_, (char*)buffer, len, MSG_DONTWAIT);
#else
    return recv(socket_, buffer, len, MSG_DONTWAIT);
#endif
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