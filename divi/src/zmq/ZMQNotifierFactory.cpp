#include <zmq/ZMQNotifierFactory.h>
#include <zmq/zmqpublishnotifier.h>

static const std::vector<std::string> notifierTypes = {
    ZMQ_MSG_HASHBLOCK,
    ZMQ_MSG_HASHTX,
    ZMQ_MSG_RAWBLOCK,
    ZMQ_MSG_RAWTX};

const std::vector<std::string>& GetZMQNotifierTypes()
{
    return notifierTypes;
}

CZMQAbstractNotifier* CreateNotifier(const std::string& notifierType)
{
    if(std::string(ZMQ_MSG_HASHBLOCK)==notifierType)
    {
        return new CZMQPublishHashBlockNotifier();
    }
    if(std::string(ZMQ_MSG_HASHTX)==notifierType)
    {
        return new CZMQPublishHashTransactionNotifier();
    }
    if(std::string(ZMQ_MSG_RAWBLOCK)==notifierType)
    {
        return new CZMQPublishRawBlockNotifier();
    }
    if(std::string(ZMQ_MSG_RAWTX)==notifierType)
    {
        return new CZMQPublishRawTransactionNotifier();
    }
    return nullptr;
}