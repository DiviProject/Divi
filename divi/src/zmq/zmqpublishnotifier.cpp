// Copyright (c) 2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "zmqpublishnotifier.h"
#include "Logging.h"
#include "crypto/common.h"
#include "BlockDiskAccessor.h"
#include <chain.h>
#include <streams.h>
#include <version.h>
#include <sync.h>
#include "primitives/block.h"
#include "primitives/transaction.h"

extern CCriticalSection cs_main;
static std::multimap<std::string, CZMQAbstractPublishNotifier*> mapPublishNotifiers;

constexpr char ZMQ_MSG_HASHBLOCK[]  = "hashblock";
constexpr char ZMQ_MSG_HASHTX[]     = "hashtx";
constexpr char ZMQ_MSG_HASHTXLOCK[] = "hashtxlock";
constexpr char ZMQ_MSG_RAWBLOCK[]   = "rawblock";
constexpr char ZMQ_MSG_RAWTX[]      = "rawtx";
constexpr char ZMQ_MSG_RAWTXLOCK[] = "rawtxlock";

static const std::vector<std::string> notifierTypes = {
    ZMQ_MSG_HASHBLOCK,
    ZMQ_MSG_HASHTX,
    ZMQ_MSG_HASHTXLOCK,
    ZMQ_MSG_RAWBLOCK,
    ZMQ_MSG_RAWTX,
    ZMQ_MSG_RAWTXLOCK};

const std::vector<std::string>& GetZMQNotifierTypes()
{
    return notifierTypes;
}

// Internal function to send multipart message
static int zmq_send_multipart(void *sock, const void* data, size_t size, ...)
{
    va_list args;
    va_start(args, size);

    while (1)
    {
        zmq_msg_t msg;

        int rc = zmq_msg_init_size(&msg, size);
        if (rc != 0)
        {
            zmqError("Unable to initialize ZMQ msg");
            return -1;
        }

        void *buf = zmq_msg_data(&msg);
        memcpy(buf, data, size);

        data = va_arg(args, const void*);

        rc = zmq_msg_send(&msg, sock, data ? ZMQ_SNDMORE : 0);
        if (rc == -1)
        {
            zmqError("Unable to send ZMQ msg");
            zmq_msg_close(&msg);
            return -1;
        }

        zmq_msg_close(&msg);

        if (!data)
            break;

        size = va_arg(args, size_t);
    }
    return 0;
}

bool CZMQAbstractPublishNotifier::Initialize(void *pcontext)
{
    assert(!psocket);

    // check if address is being used by other publish notifier
    std::multimap<std::string, CZMQAbstractPublishNotifier*>::iterator i = mapPublishNotifiers.find(address);

    if (i==mapPublishNotifiers.end())
    {
        psocket = zmq_socket(pcontext, ZMQ_PUB);
        if (!psocket)
        {
            zmqError("Failed to create socket");
            return false;
        }

        int rc = zmq_bind(psocket, address.c_str());
        if (rc!=0)
        {
            zmqError("Failed to bind address");
            zmq_close(psocket);
            return false;
        }

        // register this notifier for the address, so it can be reused for other publish notifier
        mapPublishNotifiers.insert(std::make_pair(address, this));
        return true;
    }
    else
    {
        LogPrint("zmq", "zmq: Reusing socket for address %s\n", address);

        psocket = i->second->psocket;
        mapPublishNotifiers.insert(std::make_pair(address, this));

        return true;
    }
}

void CZMQAbstractPublishNotifier::Shutdown()
{
    assert(psocket);

    int count = mapPublishNotifiers.count(address);

    // remove this notifier from the list of publishers using this address
    typedef std::multimap<std::string, CZMQAbstractPublishNotifier*>::iterator iterator;
    std::pair<iterator, iterator> iterpair = mapPublishNotifiers.equal_range(address);

    for (iterator it = iterpair.first; it != iterpair.second; ++it)
    {
        if (it->second==this)
        {
            mapPublishNotifiers.erase(it);
            break;
        }
    }

    if (count == 1)
    {
        LogPrint("zmq", "Close socket at address %s\n", address);
        int linger = 0;
        zmq_setsockopt(psocket, ZMQ_LINGER, &linger, sizeof(linger));
        zmq_close(psocket);
    }

    psocket = 0;
}

bool CZMQAbstractPublishNotifier::SendMessage(const char *command, const void* data, size_t size)
{
    assert(psocket);

    /* send three parts, command & data & a LE 4byte sequence number */
    unsigned char msgseq[sizeof(uint32_t)];
    WriteLE32(&msgseq[0], nSequence);
    int rc = zmq_send_multipart(psocket, command, strlen(command), data, size, msgseq, (size_t)sizeof(uint32_t), (void*)0);
    if (rc == -1)
        return false;

    /* increment memory only sequence number after sending */
    nSequence++;

    return true;
}

bool CZMQPublishHashBlockNotifier::NotifyBlock(const CBlockIndex *pindex)
{
    uint256 hash = pindex->GetBlockHash();
    LogPrint("zmq", "zmq: Publish hashblock %s\n", hash);
    char data[32];
    for (unsigned int i = 0; i < 32; i++)
        data[31 - i] = hash.begin()[i];
    return SendMessage(ZMQ_MSG_HASHBLOCK, data, 32);
}

bool CZMQPublishHashTransactionNotifier::NotifyTransaction(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint("zmq", "zmq: Publish hashtx %s\n", hash);
    char data[32];
    for (unsigned int i = 0; i < 32; i++)
        data[31 - i] = hash.begin()[i];
    return SendMessage(ZMQ_MSG_HASHTX, data, 32);
}

bool CZMQPublishHashTransactionLockNotifier::NotifyTransactionLock(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint("zmq", "zmq: Publish hashtxlock %s\n", hash);
    char data[32];
    for (unsigned int i = 0; i < 32; i++)
        data[31 - i] = hash.begin()[i];
    return SendMessage(ZMQ_MSG_HASHTXLOCK, data, 32);
}

bool CZMQPublishRawBlockNotifier::NotifyBlock(const CBlockIndex *pindex)
{
    LogPrint("zmq", "zmq: Publish rawblock %s\n", pindex->GetBlockHash());

// XX42    const Consensus::Params& consensusParams = Params().GetConsensus();
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    {
        LOCK(cs_main);
        CBlock block;
// XX42        if(!ReadBlockFromDisk(block, pindex, consensusParams))
        if(!ReadBlockFromDisk(block, pindex))
        {
            zmqError("Can't read block from disk");
            return false;
        }

        ss << block;
    }

    return SendMessage(ZMQ_MSG_RAWBLOCK, &(*ss.begin()), ss.size());
}

bool CZMQPublishRawTransactionNotifier::NotifyTransaction(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint("zmq", "zmq: Publish rawtx %s\n", hash);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << transaction;
    return SendMessage(ZMQ_MSG_RAWTX, &(*ss.begin()), ss.size());
}

bool CZMQPublishRawTransactionLockNotifier::NotifyTransactionLock(const CTransaction &transaction)
{
    uint256 hash = transaction.GetHash();
    LogPrint("zmq", "zmq: Publish rawtxlock %s\n", hash);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << transaction;
    return SendMessage(ZMQ_MSG_RAWTXLOCK, &(*ss.begin()), ss.size());
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
    if(std::string(ZMQ_MSG_HASHTXLOCK)==notifierType)
    {
        return new CZMQPublishHashTransactionLockNotifier();
    }
    if(std::string(ZMQ_MSG_RAWBLOCK)==notifierType)
    {
        return new CZMQPublishRawBlockNotifier();
    }
    if(std::string(ZMQ_MSG_RAWTX)==notifierType)
    {
        return new CZMQPublishRawTransactionNotifier();
    }
    if(std::string(ZMQ_MSG_RAWTXLOCK)==notifierType)
    {
        return new CZMQPublishRawTransactionLockNotifier();
    }
    return nullptr;
}