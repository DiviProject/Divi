#ifndef SOCKET_CHANNEl_H
#define SOCKET_CHANNEl_H
#include <I_CommunicationChannel.h>
#include <compat.h>
class SocketChannel final: public I_CommunicationChannel
{
private:
    SOCKET socket_;
public:
    SocketChannel(SOCKET socket);
    virtual int sendData(const void* buffer, size_t len) const;
    virtual int receiveData(void* buffer, size_t len) const;
    virtual void close();
    virtual bool isValid() const;

    SOCKET getSocket() const;
};
#endif// SOCKET_CHANNEl_H