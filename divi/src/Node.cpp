#include <Node.h>

#include <bloom.h>
#include <clientversion.h>
#include <defaultValues.h>
#include <hash.h>
#include <limitedmap.h>
#include <Logging.h>
#include <NetworkLocalAddressHelpers.h>
#include <PeerBanningService.h>
#include <random.h>
#include <Settings.h>
#include <timedata.h>
#include <utiltime.h>
#include <utilstrencodings.h>

extern Settings& settings;
uint64_t nLocalHostNonce = 0;

unsigned int SendBufferSize() { return 1000 * settings.GetArg("-maxsendbuffer", 1 * 1000); }
unsigned int ReceiveFloodSize() { return 1000 * settings.GetArg("-maxreceivebuffer", 5 * 1000); }

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

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

CNode::CNode(CNodeSignals* nodeSignals, SOCKET hSocketIn, CAddress addrIn, std::string addrNameIn, bool fInboundIn) : ssSend(SER_NETWORK, INIT_PROTO_VERSION), setAddrKnown(5000)
{
    nServices = 0;
    hSocket = hSocketIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nLastSend = 0;
    nLastRecv = 0;
    nSendBytes = 0;
    nRecvBytes = 0;
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
    fSuccessfullyConnected = false;
    fDisconnect = false;
    nRefCount = 0;
    nSendSize = 0;
    nSendOffset = 0;
    hashContinue = 0;
    nStartingHeight = -1;
    fGetAddr = false;
    fRelayTxes = false;
    setInventoryKnown.max_size(SendBufferSize() / 1000);
    pfilter = new CBloomFilter();
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    fObfuScationMaster = false;
    nodeSignals_ = nodeSignals;

    {
        LOCK(cs_nLastNodeId);
        id = nLastNodeId++;
    }

    if (ShouldLogPeerIPs())
        LogPrint("net", "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint("net", "Added connection peer=%d\n", id);

    // Be shy and don't send version until we hear
    if (hSocket != INVALID_SOCKET && !fInbound)
        PushVersion();

    assert(nodeSignals_);
    nodeSignals_->InitializeNode(GetId(), this);
}

CNode::~CNode()
{
    CloseSocket(hSocket);

    if (pfilter)
        delete pfilter;

    nodeSignals_->FinalizeNode(GetId());
}

NodeBufferStatus CNode::GetSendBufferStatus() const
{
    if(nSendSize < SendBufferSize())
    {
        return NodeBufferStatus::HAS_SPACE;
    }
    else if(nSendSize > (SendBufferSize()*2) )
    {
        return NodeBufferStatus::IS_OVERFLOWED;
    }
    else
    {
        return NodeBufferStatus::IS_FULL;
    }
}

bool CNode::IsSelfConnection(uint64_t otherNonce) const
{
    return otherNonce == nLocalHostNonce && otherNonce > 1;
}
NodeId CNode::GetId() const
{
    return id;
}
int CNode::GetRefCount()
{
    assert(nRefCount >= 0);
    return nRefCount;
}
// requires LOCK(cs_vRecvMsg)
unsigned int CNode::GetTotalRecvSize()
{
    unsigned int total = 0;
    for(const CNetMessage& msg: vRecvMsg)
        total += msg.vRecv.size() + 24;
    return total;
}
// requires LOCK(cs_vRecvMsg)
void CNode::SetRecvVersion(int nVersionIn)
{
    nRecvVersion = nVersionIn;
    for(CNetMessage& msg: vRecvMsg)
        msg.SetVersion(nVersionIn);
}
bool CNode::IsAvailableToReceive()
{
    return GetTotalRecvSize() <= ReceiveFloodSize();
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

// requires LOCK(cs_vSend)
void CNode::SocketSendData()
{
    std::deque<CSerializeData>::iterator it = vSendMsg.begin();

    while (it != vSendMsg.end()) {
        const CSerializeData& data = *it;
        assert(data.size() > nSendOffset);
        int nBytes = send(hSocket, &data[nSendOffset], data.size() - nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (nBytes > 0) {
            nLastSend = GetTime();
            nSendBytes += nBytes;
            nSendOffset += nBytes;
            NetworkUsageStats::RecordBytesSent(nBytes);
            if (nSendOffset == data.size()) {
                nSendOffset = 0;
                nSendSize -= data.size();
                it++;
            } else {
                // could not send full message; stop sending more
                break;
            }
        } else {
            if (nBytes < 0) {
                // error
                int nErr = WSAGetLastError();
                if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                    LogPrintf("socket send error %s\n", NetworkErrorString(nErr));
                    CloseSocketDisconnect();
                }
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

void CNode::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend)
{
    ENTER_CRITICAL_SECTION(cs_vSend);
    assert(ssSend.size() == 0);
    ssSend << CMessageHeader(pszCommand, 0);
    LogPrint("net", "sending: %s ", SanitizeString(pszCommand));
}

void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSend)
{
    ssSend.clear();

    LEAVE_CRITICAL_SECTION(cs_vSend);

    LogPrint("net", "(aborted)\n");
}

void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSend)
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
        Fuzz(settings.GetArg("-fuzzmessagestest", 10));

    if (ssSend.size() == 0)
        return;

    // Set the size
    unsigned int nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
    memcpy((char*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], &nSize, sizeof(nSize));

    // Set the checksum
    uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    assert(ssSend.size() >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

    LogPrint("net", "(%d bytes) peer=%d\n", nSize, id);

    std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
    ssSend.GetAndClear(*it);
    nSendSize += (*it).size();

    // If write queue empty, attempt "optimistic write"
    if (it == vSendMsg.begin())
        SocketSendData();

    LEAVE_CRITICAL_SECTION(cs_vSend);
}

void CNode::Fuzz(int nChance)
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
    Fuzz(2);
}

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char* pch, unsigned int nBytes,boost::condition_variable& messageHandlerCondition)
{
    /** Maximum length of incoming protocol messages (no message over 2 MiB is currently acceptable). */
    static const unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = 2 * 1024 * 1024;
    while (nBytes > 0) {
        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
            vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
            return false;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH) {
            LogPrint("net", "Oversized message from peer=%i, disconnecting", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete()) {
            msg.nTime = GetTimeMicros();
            messageHandlerCondition.notify_one();
        }
    }

    return true;
}

void CNode::CloseSocketDisconnect()
{
    fDisconnect = true;
    if (hSocket != INVALID_SOCKET) {
        LogPrint("net", "disconnecting peer=%d\n", id);
        CloseSocket(hSocket);
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
        vRecvMsg.clear();
}

bool CNode::DisconnectOldProtocol(int nVersionRequired, std::string strLastCommand)
{
    fDisconnect = false;
    if (nVersion < nVersionRequired) {
        LogPrintf("%s : peer=%d using obsolete version %i; disconnecting\n", __func__, id, nVersion);
        PushMessage("reject", strLastCommand, REJECT_OBSOLETE, strprintf("Version must be %d or greater", ActiveProtocol()));
        fDisconnect = true;
        PeerBanningService::Ban(GetTime(),addr);
    }

    return fDisconnect;
}

void CNode::PushVersion()
{
    int nBestHeight = nodeSignals_->GetHeight().get_value_or(0);

    /// when NTP implemented, change to just nTime = GetAdjustedTime()
    int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0", 0)));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (ShouldLogPeerIPs())
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe, addrYou, id);
    else
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe, id);
    PushMessage("version", PROTOCOL_VERSION, GetLocalServices(), nTime, addrYou, addrMe,
                nLocalHostNonce, FormatSubVersion(std::vector<std::string>()), nBestHeight, true);
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