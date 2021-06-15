#ifndef NODE_H
#define NODE_H
#include <deque>
#include <string>
#include <compat.h>
#include <streams.h>
#include <sync.h>
#include <allocators.h>
#include <protocol.h>
#include <netbase.h>
#include <uint256.h>
#include <mruset.h>
#include <stdint.h>
#include <NodeId.h>
#include <memory>
#include <I_CommunicationRegistrar.h>

#include <boost/thread/condition_variable.hpp>

class CBloomFilter;
class CNodeSignals;
class CNodeState;
class CAddrMan;

/** The maximum number of entries in an 'inv' protocol message */
constexpr unsigned int MAX_INV_SZ = 50000;

enum NodeBufferStatus
{
    HAS_SPACE,
    IS_FULL,
    IS_OVERFLOWED,
};

class CNetMessage
{
public:
    bool in_data; // parsing header (false) or data (true)

    CDataStream hdrbuf; // partially received header
    CMessageHeader hdr; // complete header
    unsigned int nHdrPos;
    CDataStream vRecv; // received message data
    unsigned int nDataPos;
    int64_t nTime; // time (in microseconds) of message receipt.

    CNetMessage(int nTypeIn, int nVersionIn);
    bool complete() const;
    void SetVersion(int nVersionIn);
    int readHeader(const char* pch, unsigned int nBytes);
    int readData(const char* pch, unsigned int nBytes);
};

class NetworkMessageSerializer
{
public:
    static void BeginMessage(CDataStream& dataStream, const char* pszCommand);
    static void EndMessage(CDataStream& dataStream, unsigned& dataSize);

    static void SerializeNextArgument(CDataStream& dataStream)
    {
    }
    template <typename T1, typename ...Args>
    static void SerializeNextArgument(CDataStream& dataStream,const T1& nextArgument, Args&&... args)
    {
        dataStream << nextArgument;
        SerializeNextArgument(dataStream,std::forward<Args>(args)...);
    }
    enum DeserializationStatus
    {
        MISSING_DATA,
        FAILURE,
        SUCCESS,
    };
    static DeserializationStatus DeserializeNetworkMessageFromBuffer(const char*& buffer,unsigned& bytes,CNetMessage& msg);
};

class CommunicationLogger
{
private:
    int64_t nLastSend;
    int64_t nLastRecv;
    uint64_t nSendBytes;
    uint64_t nRecvBytes;

public:
    CommunicationLogger();
    void RecordSentBytes(int additionalBytes);
    void RecordReceivedBytes(int additionalBytes);
    int64_t GetLastTimeDataSent() const;
    int64_t GetLastTimeDataReceived() const;
    uint64_t GetTotalBytesReceived() const;
    uint64_t GetTotalBytesSent() const;
};

class I_CommunicationChannel
{
public:
    ~I_CommunicationChannel(){}
    virtual int sendData(const void* buffer, size_t len) const = 0;
    virtual int receiveData(void* buffer, size_t len) const = 0;
    virtual void close() = 0;
    virtual bool isValid() const = 0;
};

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

class QueuedMessageConnection
{
private:
    const bool& fSuccessfullyConnected_;
    CommunicationLogger& dataLogger_;

    CDataStream ssSend;
    std::deque<CSerializeData> vSendMsg;
    CCriticalSection cs_vSend;

    std::deque<CNetMessage> vRecvMsg;
    CCriticalSection cs_vRecvMsg;

    SocketChannel channel_;
    size_t nSendSize;   // total size of all vSendMsg entries
    size_t nSendOffset; // offset inside the first vSendMsg already sent

    int nRecvVersion;
    bool fDisconnect;

    // TODO: Document the postcondition of this function.  Is cs_vSend locked?
    void BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend);
    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void AbortMessage() UNLOCK_FUNCTION(cs_vSend);
    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void EndMessage(unsigned int& messageDataSize) UNLOCK_FUNCTION(cs_vSend);

protected:
    size_t GetSendBufferSize() const;

private:
    void SendData();
    void ReceiveData(boost::condition_variable& messageHandlerCondition);
    bool IsAvailableToReceive();
    bool ConvertDataBufferToNetworkMessage(const char* pch, unsigned int nBytes,boost::condition_variable& messageHandlerCondition);
    unsigned int GetTotalRecvSize();

public:
    template <typename ...Args>
    void PushMessageAndRecordDataSize(unsigned int& messageDataSize, const char* pszCommand, Args&&... args)
    {
        try {
            BeginMessage(pszCommand);
            NetworkMessageSerializer::SerializeNextArgument(ssSend,std::forward<Args>(args)...);
            EndMessage(messageDataSize);
        } catch (...) {
            AbortMessage();
            throw;
        }
    }

    QueuedMessageConnection(
        SOCKET hSocketIn,
        const bool& fSuccessfullyConnected,
        CommunicationLogger& dataLogger);
    bool CommunicationChannelIsValid() const;
    void CloseCommsChannel();
    void CloseCommsAndDisconnect();
    void RegisterCommunication(I_CommunicationRegistrar<SOCKET>& registrar);
    bool TrySendData(const I_CommunicationRegistrar<SOCKET>& registrar);
    bool TryReceiveData(const I_CommunicationRegistrar<SOCKET>& registrar, boost::condition_variable& messageHandlerCondition);

    bool SendAndReceiveBuffersAreEmpty() const;
    bool HasReceivedACompleteMessage() const;
    CCriticalSection& GetSendLock();
    CCriticalSection& GetReceiveLock();
    NodeBufferStatus GetSendBufferStatus() const;

    void SetInboundSerializationVersion(int versionNumber);
    void SetOutboundSerializationVersion(int versionNumber);
    bool IsFlaggedForDisconnection() const;
    void FlagForDisconnection();
    std::deque<CNetMessage>& GetReceivedMessageQueue();
};

class CNode
{
public:
    bool fSuccessfullyConnected;
    CommunicationLogger dataLogger;
private:
    QueuedMessageConnection messageConnection_;
public:
    bool CommunicationChannelIsValid() const;
    void CloseCommsAndDisconnect();
    void RegisterCommunication(I_CommunicationRegistrar<SOCKET>& registrar);
    bool TrySendData(const I_CommunicationRegistrar<SOCKET>& registrar);
    bool TryReceiveData(const I_CommunicationRegistrar<SOCKET>& registrar, boost::condition_variable& messageHandlerCondition);
    NodeBufferStatus GetSendBufferStatus() const;
    void SetInboundSerializationVersion(int versionNumber);
    void SetOutboundSerializationVersion(int versionNumber);
    bool IsFlaggedForDisconnection() const;
    void FlagForDisconnection();
    std::deque<CNetMessage>& GetReceivedMessageQueue();

private:
    std::deque<CInv> vRecvGetData;
public:
    uint64_t nServices;
    int64_t nTimeConnected;
    CAddress addr;
    std::string addrName;
    CService addrLocal;
    int nVersion;
    // strSubVer is whatever byte array we read from the wire. However, this field is intended
    // to be printed out, displayed to humans in various forms and so on. So we sanitize it and
    // store the sanitized version in cleanSubVer. The original should be used when dealing with
    // the network or wire types and the cleaned string used when displayed or logged.
    std::string strSubVer, cleanSubVer;
    bool fWhitelisted; // This peer can bypass DoS banning.
    bool fOneShot;
    bool fClient;
    bool fInbound;
    bool fNetworkNode;
    // We use fRelayTxes for two purposes -
    // a) it allows us to not relay tx invs before receiving the peer's version message
    // b) the peer may tell us in their version message that we should not relay tx invs
    //    until they have initialized their bloom filter.
    bool fRelayTxes;
    // Should be 'true' only if we connected to this node to actually mix funds.
    // In this case node will be released automatically via CMasternodeMan::ProcessMasternodeConnections().
    // Connecting to verify connectability/status or connecting for sending/relaying single message
    // (even if it's relative to mixing e.g. for blinding) should NOT set this to 'true'.
    // For such cases node should be released manually (preferably right after corresponding code).
    CCriticalSection cs_filter;
    CBloomFilter* pfilter;
    int nRefCount;
    NodeId id;

    int nSporksCount = -1;

protected:
    // Basic fuzz-testing
    void Fuzz(int nChance); // modifies ssSend

    CNodeSignals* nodeSignals_;
    std::unique_ptr<CNodeState> nodeState_;
public:
    uint256 hashContinue;
    int nStartingHeight;

    // flood relay
    std::vector<CAddress> vAddrToSend;
    mruset<CAddress> setAddrKnown;
    bool fGetAddr;
    std::set<uint256> setKnown;

    // inventory based relay
    mruset<CInv> setInventoryKnown;
    std::vector<CInv> vInventoryToSend;
    CCriticalSection cs_inventory;
    std::multimap<int64_t, CInv> mapAskFor;
    std::vector<uint256> vBlockRequested;

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    uint64_t nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    int64_t nPingUsecStart;
    // Last measured round-trip time.
    int64_t nPingUsecTime;
    // Whether a ping is requested.
    bool fPingQueued;

    int nSporksSynced = 0;

    CNode(CNodeSignals* nodeSignals, CAddrMan& addressMananger, SOCKET hSocketIn, CAddress addrIn, std::string addrNameIn = "", bool fInboundIn = false);
    ~CNode();

    void LogMessageSize(unsigned int messageDataSize) const;
public:
    template <typename ...Args>
    void PushMessage(const char* pszCommand, Args&&... args)
    {
        unsigned int messageDataSize = 0u;
        messageConnection_.PushMessageAndRecordDataSize(messageDataSize,pszCommand,std::forward<Args>(args)...);
        LogMessageSize(messageDataSize);
    }

    void ProcessReceiveMessages(bool& shouldSleep);
    void ProcessSendMessages(bool trickle);
    void AdvertizeLocalAddress(int64_t rebroadcastTimestamp);
    bool IsInUse();
    void MaybeSendPing();

    bool RespondToRequestForData();
    void RecordRequestForData(std::vector<CInv>& inventoryRequested);
    std::deque<CInv>& GetRequestForDataQueue();

    void CheckForInnactivity();
    bool CanBeDisconnected() const;
    CNodeState* GetNodeState();
    void UpdatePreferredDownloadStatus();
    void SetToCurrentlyConnected();
    bool IsSelfConnection(uint64_t otherNonce) const;
    NodeId GetId() const;
    int GetRefCount() const;

    CNode* AddRef();
    void Release();
    void AddAddressKnown(const CAddress& addr);
    void PushAddress(const CAddress& addr);
    void AddInventoryKnown(const CInv& inv);
    void PushInventory(const CInv& inv);
    void AskFor(const CInv& inv);

    void PushVersion(int currentChainTipHeight);
    void SetSporkCount(int nSporkCountIn);
    bool AreSporksSynced() const;

    bool DisconnectOldProtocol(int nVersionRequired, std::string strLastCommand = "");
    bool CanSendMessagesToPeer() const;
    bool IsPreferredDownloadSource() const;
    static void ClearInventoryItem(const CInv& inv);
};

class NetworkUsageStats
{
private:
    // Network usage totals
    static CCriticalSection cs_totalBytesRecv;
    static CCriticalSection cs_totalBytesSent;
    static uint64_t nTotalBytesRecv;
    static uint64_t nTotalBytesSent;
public:
    // Network stats
    static void RecordBytesRecv(uint64_t bytes);
    static void RecordBytesSent(uint64_t bytes);

    static uint64_t GetTotalBytesRecv();
    static uint64_t GetTotalBytesSent();
};
#endif// NODE_H