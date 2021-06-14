#include <Node.h>

#include <bloom.h>
#include <clientversion.h>
#include <defaultValues.h>
#include <hash.h>
#include <limitedmap.h>
#include <Logging.h>
#include <NetworkLocalAddressHelpers.h>
#include <random.h>
#include <Settings.h>
#include <timedata.h>
#include <utiltime.h>
#include <utilstrencodings.h>
#include <NodeSignals.h>
#include <NodeState.h>

extern Settings& settings;
uint64_t nLocalHostNonce = 0;

// These quantities are measured in bytes
unsigned int MaxSendBufferSize() { return 1000 * settings.GetArg("-maxsendbuffer", 1 * 1000); }
unsigned int MaxReceiveBufferSize() { return 1000 * settings.GetArg("-maxreceivebuffer", 5 * 1000); }

CNetMessage::CNetMessage(int nTypeIn, int nVersionIn) : hdrbuf(nTypeIn, nVersionIn), vRecv(nTypeIn, nVersionIn)
{
    hdrbuf.resize(24);
    in_data = false;
    nHdrPos = 0;
    nDataPos = 0;
    nTime = 0;
}

bool CNetMessage::complete() const
{
    if (!in_data)
        return false;
    return (hdr.nMessageSize == nDataPos);
}

void CNetMessage::SetVersion(int nVersionIn)
{
    hdrbuf.SetVersion(nVersionIn);
    vRecv.SetVersion(nVersionIn);
}

int CNetMessage::readHeader(const char* pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    } catch (const std::exception&) {
        return -1;
    }

    // reject messages larger than MAX_SIZE
    if (hdr.nMessageSize > MAX_SIZE)
        return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char* pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy) {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}

void NetworkMessageSerializer::BeginMessage(CDataStream& dataStream,const char* pszCommand)
{
    assert(dataStream.size() == 0);
    dataStream << CMessageHeader(pszCommand, 0);
    LogPrint("net", "sending: %s ", SanitizeString(pszCommand));
}
void NetworkMessageSerializer::EndMessage(CDataStream& dataStream, unsigned& dataSize)
{
    if (dataStream.size() == 0)
        return;

    // Set the size
    dataSize = dataStream.size() - CMessageHeader::HEADER_SIZE;
    memcpy((char*)&dataStream[CMessageHeader::MESSAGE_SIZE_OFFSET], &dataSize, sizeof(dataSize));

    // Set the checksum
    uint256 hash = Hash(dataStream.begin() + CMessageHeader::HEADER_SIZE, dataStream.end());
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    assert(dataStream.size() >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char*)&dataStream[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));
}

NetworkMessageSerializer::DeserializationStatus NetworkMessageSerializer::DeserializeNetworkMessageFromBuffer(const char*& buffer,unsigned& bytes,CNetMessage& msg)
{
    constexpr unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 2 * 1024 * 1024;
    while (bytes > 0) {
        // get current incomplete message, or create a new one

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(buffer, bytes);
        else
            handled = msg.readData(buffer, bytes);

        if (handled < 0)
            return FAILURE;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH) {
            LogPrint("net", "Oversized message from peer, disconnecting");
            return FAILURE;
        }

        buffer += handled;
        bytes -= handled;

        if (msg.complete()) return SUCCESS;
    }

    return MISSING_DATA;
}

CommunicationLogger::CommunicationLogger(
    ): nLastSend(0)
    , nLastRecv(0)
    , nSendBytes(0)
    , nRecvBytes(0)
{
}
void CommunicationLogger::RecordSentBytes(int additionalBytes)
{
    nLastSend = GetTime();
    nSendBytes += additionalBytes;
    NetworkUsageStats::RecordBytesSent(additionalBytes);
}
void CommunicationLogger::RecordReceivedBytes(int additionalBytes)
{
    nLastRecv = GetTime();
    nRecvBytes += additionalBytes;
    NetworkUsageStats::RecordBytesRecv(additionalBytes);
}
int64_t CommunicationLogger::GetLastTimeDataSent() const
{
    return nLastSend;
}
int64_t CommunicationLogger::GetLastTimeDataReceived() const
{
    return nLastRecv;
}
uint64_t CommunicationLogger::GetTotalBytesReceived() const
{
    return nRecvBytes;
}
uint64_t CommunicationLogger::GetTotalBytesSent() const
{
    return nSendBytes;
}

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

static bool SocketHasErrors(bool shouldLogError)
{
    int nErr = WSAGetLastError();
    if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)
    {
        if (shouldLogError)
            LogPrintf("socket recv error %s\n", NetworkErrorString(nErr));
        return true;
    }
    return false;
}

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

QueuedMessageConnection::QueuedMessageConnection(
    SOCKET hSocketIn
    ): fSuccessfullyConnected(false)
    , dataLogger()
    , ssSend(SER_NETWORK, INIT_PROTO_VERSION)
    , vSendMsg()
    , cs_vSend()
    , vRecvMsg()
    , cs_vRecvMsg()
    , channel_(hSocketIn)
    , nSendSize(0)
    , nSendOffset(0)
    , nRecvVersion(INIT_PROTO_VERSION)
    , fDisconnect(false)
{
}

void QueuedMessageConnection::CloseCommsChannel()
{
    channel_.close();
}

// requires LOCK(cs_vSend)
void QueuedMessageConnection::SendData()
{
    AssertLockHeld(cs_vSend);
    std::deque<CSerializeData>::iterator it = vSendMsg.begin();

    while (it != vSendMsg.end()) {
        const CSerializeData& data = *it;
        assert(data.size() > nSendOffset);
        int nBytes = channel_.sendData(&data[nSendOffset], data.size() - nSendOffset);
        if (nBytes > 0) {
            dataLogger.RecordSentBytes(nBytes);
            nSendOffset += nBytes;
            if (nSendOffset == data.size()) {
                nSendOffset = 0;
                nSendSize -= data.size();
                it++;
            } else {
                // could not send full message; stop sending more
                break;
            }
        }
        else
        {
            if (nBytes < 0 && SocketHasErrors(true))
            {
                // error
                CloseCommsAndDisconnect();
            }
            // couldn't send anything at all
            break;
        }
    }

    if (it == vSendMsg.end()) {
        assert(nSendOffset == 0);
        assert(nSendSize == 0);
    }
    vSendMsg.erase(vSendMsg.begin(), it);
}

// Requires LOCK(cs_vRecvMsg)
void QueuedMessageConnection::ReceiveData(boost::condition_variable& messageHandlerCondition)
{
    AssertLockHeld(cs_vRecvMsg);
    // typical socket buffer is 8K-64K
    char pchBuf[0x10000];
    int nBytes = channel_.receiveData(pchBuf, sizeof(pchBuf));
    if (nBytes > 0) {
        if (!ConvertDataBufferToNetworkMessage(pchBuf, nBytes,messageHandlerCondition))
            CloseCommsAndDisconnect();
        dataLogger.RecordReceivedBytes(nBytes);
    } else if (nBytes == 0) {
        // socket closed gracefully
        if (!fDisconnect)
            LogPrint("net", "socket closed\n");
        CloseCommsAndDisconnect();
    }
    else if (nBytes < 0 && SocketHasErrors(!fDisconnect))
    {
        // error
        CloseCommsAndDisconnect();
    }
}
void QueuedMessageConnection::RegisterCommunication(I_CommunicationRegistrar<SOCKET>& registrar)
{
    registrar.RegisterForErrors(channel_.getSocket());
    {
        TRY_LOCK(cs_vSend, lockSend);
        if (lockSend && !vSendMsg.empty()) {
            registrar.RegisterForSend(channel_.getSocket());
            return;
        }
    }
    {
        TRY_LOCK(cs_vRecvMsg, lockRecv);
        if (lockRecv && IsAvailableToReceive())
        {
            registrar.RegisterForReceive(channel_.getSocket());
        }
    }
}
bool QueuedMessageConnection::CommunicationChannelIsValid() const
{
    return channel_.isValid();
}

bool QueuedMessageConnection::TrySendData(const I_CommunicationRegistrar<SOCKET>& registrar)
{
    if (!CommunicationChannelIsValid())
        return false;
    if (registrar.IsRegisteredForSend(channel_.getSocket())) {
        TRY_LOCK(cs_vSend, lockSend);
        if (lockSend)
            SendData();
    }
    return true;
}
bool QueuedMessageConnection::TryReceiveData(const I_CommunicationRegistrar<SOCKET>& registrar, boost::condition_variable& messageHandlerCondition)
{
    if (!CommunicationChannelIsValid())
        return false;
    if (registrar.IsRegisteredForReceive(channel_.getSocket()) || registrar.IsRegisteredForErrors(channel_.getSocket()))
    {
        TRY_LOCK(cs_vRecvMsg, lockRecv);
        if (lockRecv)
            ReceiveData(messageHandlerCondition);
    }
    return true;
}

void QueuedMessageConnection::CloseCommsAndDisconnect()
{
    fDisconnect = true;
    if (channel_.isValid()) {
        LogPrint("net", "disconnecting peer\n");
        CloseCommsChannel();
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
        vRecvMsg.clear();
}
bool QueuedMessageConnection::IsAvailableToReceive()
{
    AssertLockHeld(cs_vRecvMsg);
    return vRecvMsg.empty() || !vRecvMsg.front().complete() || GetTotalRecvSize() <= MaxReceiveBufferSize();
}
// requires LOCK(cs_vRecvMsg)
unsigned int QueuedMessageConnection::GetTotalRecvSize()
{
    AssertLockHeld(cs_vRecvMsg);
    unsigned int total = 0;
    for(const CNetMessage& msg: vRecvMsg)
        total += msg.vRecv.size() + 24;
    return total;
}
// requires LOCK(cs_vRecvMsg)
bool QueuedMessageConnection::ConvertDataBufferToNetworkMessage(const char* pch, unsigned int nBytes,boost::condition_variable& messageHandlerCondition)
{
    AssertLockHeld(cs_vRecvMsg);
    /** Maximum length of incoming protocol messages (no message over 2 MiB is currently acceptable). */
    while (nBytes > 0) {
        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() || vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        auto deserializationStatus = NetworkMessageSerializer::DeserializeNetworkMessageFromBuffer(pch,nBytes,msg);
        if(deserializationStatus == NetworkMessageSerializer::SUCCESS)
        {
            msg.nTime = GetTimeMicros();
            messageHandlerCondition.notify_one();
        }
        else if(deserializationStatus == NetworkMessageSerializer::FAILURE)
        {
            return false;
        }
    }

    return true;
}
bool QueuedMessageConnection::IsFlaggedForDisconnection() const
{
    return fDisconnect;
}
void QueuedMessageConnection::FlagForDisconnection()
{
    fDisconnect = true;
}
size_t QueuedMessageConnection::GetSendBufferSize() const
{
    return nSendSize;
}
bool QueuedMessageConnection::SendAndReceiveBuffersAreEmpty() const
{
    return vRecvMsg.empty() && ssSend.empty();
}
NodeBufferStatus QueuedMessageConnection::GetSendBufferStatus() const
{
    const size_t sendBufferSize = GetSendBufferSize();
    if(sendBufferSize < MaxSendBufferSize())
    {
        return NodeBufferStatus::HAS_SPACE;
    }
    else if(sendBufferSize > (MaxSendBufferSize()*2) )
    {
        return NodeBufferStatus::IS_OVERFLOWED;
    }
    else
    {
        return NodeBufferStatus::IS_FULL;
    }
}

void QueuedMessageConnection::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend)
{
    ENTER_CRITICAL_SECTION(cs_vSend);
    NetworkMessageSerializer::BeginMessage(ssSend,pszCommand);
}

void QueuedMessageConnection::AbortMessage() UNLOCK_FUNCTION(cs_vSend)
{
    ssSend.clear();
    LEAVE_CRITICAL_SECTION(cs_vSend);
    LogPrint("net", "(aborted)\n");
}


static void Fuzz(int nChance,bool& fSuccessfullyConnected, CDataStream& ssSend)
{
    if (!fSuccessfullyConnected) return; // Don't fuzz initial handshake
    if (GetRand(nChance) != 0) return;   // Fuzz 1 of every nChance messages

    switch (GetRand(3)) {
    case 0:
        // xor a random byte with a random value:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend[pos] ^= (unsigned char)(GetRand(256));
        }
        break;
    case 1:
        // delete a random byte:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend.erase(ssSend.begin() + pos);
        }
        break;
    case 2:
        // insert a random byte at a random position
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            char ch = (char)GetRand(256);
            ssSend.insert(ssSend.begin() + pos, ch);
        }
        break;
    }
    // Chance of more than one change half the time:
    // (more changes exponentially less likely):
    Fuzz(2,fSuccessfullyConnected,ssSend);
}

void QueuedMessageConnection::EndMessage(unsigned int& messageDataSize) UNLOCK_FUNCTION(cs_vSend)
{
    // The -*messagestest options are intentionally not documented in the help message,
    // since they are only used during development to debug the networking code and are
    // not intended for end-users.
    if (settings.ParameterIsSet("-dropmessagestest") && GetRand(settings.GetArg("-dropmessagestest", 2)) == 0) {
        LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
        AbortMessage();
        return;
    }
    if (settings.ParameterIsSet("-fuzzmessagestest"))
        Fuzz(settings.GetArg("-fuzzmessagestest", 10),fSuccessfullyConnected,ssSend);

    if (ssSend.size() == 0)
        return;

    // Set the size
    NetworkMessageSerializer::EndMessage(ssSend,messageDataSize);

    std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
    ssSend.GetAndClear(*it);
    nSendSize += (*it).size();

    // If write queue empty, attempt "optimistic write"
    if (it == vSendMsg.begin())
        SendData();

    LEAVE_CRITICAL_SECTION(cs_vSend);
}

std::deque<CNetMessage>& QueuedMessageConnection::GetReceivedMessageQueue()
{
    AssertLockHeld(cs_vRecvMsg);
    return vRecvMsg;
}
void QueuedMessageConnection::SetInboundSerializationVersion(int versionNumber)
{
    AssertLockHeld(cs_vRecvMsg);
    nRecvVersion = versionNumber;
    for(CNetMessage& msg: vRecvMsg)
        msg.SetVersion(versionNumber);
}
void QueuedMessageConnection::SetOutboundSerializationVersion(int versionNumber)
{
    ssSend.SetVersion(versionNumber);
}

CNode::CNode(
    CNodeSignals* nodeSignals,
    CAddrMan& addressMananger,
    SOCKET hSocketIn,
    CAddress addrIn,
    std::string addrNameIn,
    bool fInboundIn
    ) : QueuedMessageConnection(hSocketIn)
    , vRecvGetData()
    , setAddrKnown(5000)
{
    nServices = 0;
    nTimeConnected = GetTime();
    addr = addrIn;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    fClient = false; // set by version message
    fInbound = fInboundIn;
    fNetworkNode = false;
    nRefCount = 0;
    hashContinue = 0;
    nStartingHeight = -1;
    fGetAddr = false;
    fRelayTxes = false;
    setInventoryKnown.max_size(MaxSendBufferSize() / 1000);
    pfilter = new CBloomFilter();
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    nodeSignals_ = nodeSignals;

    {
        LOCK(cs_nLastNodeId);
        id = nLastNodeId++;
    }

    if (ShouldLogPeerIPs())
        LogPrint("net", "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint("net", "Added connection peer=%d\n", id);

    assert(nodeSignals_);
    nodeState_.reset(new CNodeState(id,addressMananger));
    nodeState_->name = addrName;
    nodeState_->address = addr;
    nodeSignals_->InitializeNode(*nodeState_);
}

CNode::~CNode()
{
    CloseCommsChannel();

    if (pfilter)
        delete pfilter;

    // Network connections can be unused (nRefCount=0), connected (nRefCount=1), in-use (nRefCount > 1)
    assert(nRefCount<2);
    nodeSignals_->FinalizeNode(id);
    nodeState_->Finalize();
    nodeState_.reset();
}

void CNode::LogMessageSize(unsigned int messageDataSize) const
{
    LogPrint("net", "(%d bytes) peer=%d\n", messageDataSize, id);
}

void CNode::ProcessReceiveMessages(bool& shouldSleep)
{
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
    {
        bool result = *(nodeSignals_->ProcessReceivedMessages(this));
        if (!result)
            CloseCommsAndDisconnect();

        if (GetSendBufferStatus()==NodeBufferStatus::HAS_SPACE)
        {
            if (!vRecvGetData.empty() || (!vRecvMsg.empty() && vRecvMsg[0].complete()))
            {
                shouldSleep = false;
            }
        }
    }
}
void CNode::ProcessSendMessages(bool trickle)
{
    TRY_LOCK(cs_vSend, lockSend);
    if(lockSend)
    {
        nodeSignals_->SendMessages(this,trickle || fWhitelisted);
    }
}
bool CNode::RespondToRequestForData()
{
    if (!vRecvGetData.empty())
        nodeSignals_->RespondToRequestForDataFrom(this);

    // this maintains the order of responses
    if (!vRecvGetData.empty()) return false;

    return true;
}
void CNode::RecordRequestForData(std::vector<CInv>& inventoryRequested)
{
    LogPrint("net", "received getdata (%u invsz) peer=%d\n", inventoryRequested.size(), id);

    if (inventoryRequested.size() > 0)
        LogPrint("net", "received getdata for: %s peer=%d\n", inventoryRequested[0], id);

    vRecvGetData.insert(vRecvGetData.end(), inventoryRequested.begin(), inventoryRequested.end());
}
std::deque<CInv>& CNode::GetRequestForDataQueue()
{
    return vRecvGetData;
}

void CNode::CheckForInnactivity()
{
    /** Time after which to disconnect, after waiting for a ping response (or inactivity). */
    constexpr int TIMEOUT_INTERVAL = 20 * 60;
    int64_t nTime = GetTime();
    int64_t lastTimeDataSent = dataLogger.GetLastTimeDataSent();
    int64_t lastTimeDataReceived = dataLogger.GetLastTimeDataReceived();
    if (nTime - nTimeConnected > 60) {
        if (lastTimeDataReceived == 0 || lastTimeDataSent == 0) {
            LogPrint("net", "socket no message in first 60 seconds, %d %d from %d\n", lastTimeDataReceived != 0, lastTimeDataSent != 0, id);
            FlagForDisconnection();
        } else if (nTime - lastTimeDataSent > TIMEOUT_INTERVAL) {
            LogPrintf("socket sending timeout: %is\n", nTime - lastTimeDataSent);
            FlagForDisconnection();
        } else if (nTime - lastTimeDataReceived > (nVersion > BIP0031_VERSION ? TIMEOUT_INTERVAL : 90 * 60)) {
            LogPrintf("socket receive timeout: %is\n", nTime - lastTimeDataReceived);
            FlagForDisconnection();
        } else if (nPingNonceSent && nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros()) {
            LogPrintf("ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - nPingUsecStart));
            FlagForDisconnection();
        }
    }
}

void CNode::AdvertizeLocalAddress(int64_t rebroadcastTimestamp)
{
    TRY_LOCK(cs_vSend, lockSend);
    if (!lockSend) return;

    // Periodically clear setAddrKnown to allow refresh broadcasts
    if (rebroadcastTimestamp > 0)
        setAddrKnown.clear();

    // Rebroadcast our address
    nodeSignals_->AdvertizeLocalAddress(this);
}

bool CNode::IsInUse()
{
    if (GetRefCount() <= 0)
    {
        {
            TRY_LOCK(cs_vSend, lockSend);
            if (lockSend)
            {
                TRY_LOCK(cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    TRY_LOCK(cs_inventory, lockInv);
                    if (lockInv)
                        return false;
                }
            }
        }
    }
    return true;
}
bool CNode::CanBeDisconnected() const
{
    return GetRefCount() <= 0 && SendAndReceiveBuffersAreEmpty();
}

CNodeState* CNode::GetNodeState()
{
    return nodeState_.get();
}
void CNode::UpdatePreferredDownloadStatus()
{
    nodeState_->UpdatePreferredDownload(IsPreferredDownloadSource());
}
void CNode::SetToCurrentlyConnected()
{
    nodeState_->fCurrentlyConnected = true;
}

bool CNode::IsSelfConnection(uint64_t otherNonce) const
{
    return otherNonce == nLocalHostNonce && otherNonce > 1;
}
NodeId CNode::GetId() const
{
    return id;
}
int CNode::GetRefCount() const
{
    assert(nRefCount >= 0);
    return nRefCount;
}

CNode* CNode::AddRef()
{
    nRefCount++;
    return this;
}
void CNode::Release()
{
    nRefCount--;
}
void CNode::AddAddressKnown(const CAddress& addr)
{
    setAddrKnown.insert(addr);
}
void CNode::AddInventoryKnown(const CInv& inv)
{
    {
        LOCK(cs_inventory);
        setInventoryKnown.insert(inv);
    }
}
void CNode::PushInventory(const CInv& inv)
{
    LOCK(cs_inventory);
    if (setInventoryKnown.count(inv) == 0)
        vInventoryToSend.push_back(inv);
}

void CNode::PushAddress(const CAddress& addr)
{
    /** The maximum number of new addresses to accumulate before announcing. */
    static const unsigned int MAX_ADDR_TO_SEND = 1000;
    // Known checking here is only to save space from duplicates.
    // SendMessages will filter it again for knowns that were added
    // after addresses were pushed.
    if (addr.IsValid() && !setAddrKnown.count(addr)) {
        if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
            vAddrToSend[FastRandomContext()(vAddrToSend.size())] = addr;
        } else {
            vAddrToSend.push_back(addr);
        }
    }
}

void CNode::AskFor(const CInv& inv)
{
    /** The maximum number of entries in mapAskFor */
    static const size_t MAPASKFOR_MAX_SZ = MAX_INV_SZ;
    if (mapAskFor.size() > MAPASKFOR_MAX_SZ)
        return;
    // We're using mapAskFor as a priority queue,
    // the key is the earliest time the request can be sent
    int64_t nRequestTime;
    limitedmap<CInv, int64_t>::const_iterator it = mapAlreadyAskedFor.find(inv);
    if (it != mapAlreadyAskedFor.end())
        nRequestTime = it->second;
    else
        nRequestTime = 0;
    LogPrint("net", "askfor %s  %d (%s) peer=%d\n", inv, nRequestTime, DateTimeStrFormat("%H:%M:%S", nRequestTime / 1000000), id);

    // Make sure not to reuse time indexes to keep things in the same order
    int64_t nNow = GetTimeMicros() - 1000000;
    static int64_t nLastTime;
    ++nLastTime;
    nNow = std::max(nNow, nLastTime);
    nLastTime = nNow;

    // Each retry is 2 minutes after the last
    nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
    if (it != mapAlreadyAskedFor.end())
        mapAlreadyAskedFor.update(it, nRequestTime);
    else
        mapAlreadyAskedFor.insert(std::make_pair(inv, nRequestTime));
    mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

bool CNode::DisconnectOldProtocol(int nVersionRequired, std::string strLastCommand)
{
    if (!IsFlaggedForDisconnection() && nVersion < nVersionRequired) {
        LogPrintf("%s : peer=%d using obsolete version %i; disconnecting\n", __func__, id, nVersion);
        PushMessage("reject", strLastCommand, REJECT_OBSOLETE, strprintf("Version must be %d or greater", ActiveProtocol()));
        FlagForDisconnection();
    }

    return IsFlaggedForDisconnection();
}

void CNode::PushVersion(int currentChainTipHeight)
{
    /// when NTP implemented, change to just nTime = GetAdjustedTime()
    int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0", 0)));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (ShouldLogPeerIPs())
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, currentChainTipHeight, addrMe, addrYou, id);
    else
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, currentChainTipHeight, addrMe, id);
    PushMessage("version", PROTOCOL_VERSION, GetLocalServices(), nTime, addrYou, addrMe,
                nLocalHostNonce, FormatSubVersion(std::vector<std::string>()), currentChainTipHeight, true);
}

void CNode::SetSporkCount(int nSporkCountIn)
{
    if(nSporkCountIn > 0) {
        nSporksCount = nSporkCountIn;
    }
    else {
        nSporksCount = 0;
    }
}

bool CNode::AreSporksSynced() const
{
    return nSporksCount >= 0 && nSporksCount <= nSporksSynced;
}

/** Checks whether we want to send messages to this peer.  We do not
 *  send messages until we receive their version and get sporks.  */
bool CNode::CanSendMessagesToPeer() const
{
    // Don't send anything until we get their version message
    if (nVersion == 0)
        return false;

    // Don't send anything until we get sporks from peer
    if(!fInbound && !AreSporksSynced())
        return false;

    return true;
}

    /** Checks if we should send a ping message to this peer, and does it
     *  if we should.  */
void CNode::MaybeSendPing()
{
    TRY_LOCK(cs_vSend, lockSend);
    if (!lockSend) return;
    /** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
    static const int PING_INTERVAL = 2 * 60;
    bool pingSend = false;
    if (fPingQueued) {
        // RPC ping request by user
        pingSend = true;
    }
    if (nPingNonceSent == 0 && nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
        // Ping automatically sent as a latency probe & keepalive.
        pingSend = true;
    }
    if (pingSend) {
        uint64_t nonce = 0;
        while (nonce == 0) {
            GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
        }
        fPingQueued = false;
        nPingUsecStart = GetTimeMicros();
        if (nVersion > BIP0031_VERSION) {
            nPingNonceSent = nonce;
            PushMessage("ping", nonce);
        } else {
            // Peer is too old to support ping command with nonce, pong will never arrive.
            nPingNonceSent = 0;
            PushMessage("ping");
        }
    }
}

bool CNode::IsPreferredDownloadSource() const
{
    return (!fInbound || fWhitelisted) && !fOneShot && !fClient;
}

void CNode::ClearInventoryItem(const CInv& inv)
{
    mapAlreadyAskedFor.erase(inv);
}

uint64_t NetworkUsageStats::nTotalBytesRecv = 0;
uint64_t NetworkUsageStats::nTotalBytesSent = 0;
CCriticalSection NetworkUsageStats::cs_totalBytesRecv;
CCriticalSection NetworkUsageStats::cs_totalBytesSent;
void NetworkUsageStats::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void NetworkUsageStats::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;
}

uint64_t NetworkUsageStats::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t NetworkUsageStats::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}