#ifndef I_COMMUNICATION_REGISTRAR_H
#define I_COMMUNICATION_REGISTRAR_H
template <typename T>
class I_CommunicationRegistrar
{
public:
    virtual ~I_CommunicationRegistrar(){}
    virtual void RegisterForErrors(T identifier) = 0;
    virtual void RegisterForSend(T identifier) = 0;
    virtual void RegisterForReceive(T identifier) = 0;
    virtual bool IsRegisteredForErrors(T identifier) const = 0;
    virtual bool IsRegisteredForSend(T identifier) const = 0;
    virtual bool IsRegisteredForReceive(T identifier) const = 0;
};
#endif// I_COMMUNICATION_REGISTRAR_H