#ifndef I_COMMUNICATION_CHANNEL_H
#define I_COMMUNICATION_CHANNEL_H
#include <cstdlib>
class I_CommunicationChannel
{
public:
    virtual ~I_CommunicationChannel(){}
    virtual int sendData(const void* buffer, size_t len) const = 0;
    virtual int receiveData(void* buffer, size_t len) const = 0;
    virtual void close() = 0;
    virtual bool isValid() const = 0;
};
#endif// I_COMMUNICATION_CHANNEL_H