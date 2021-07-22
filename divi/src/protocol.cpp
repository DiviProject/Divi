// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "protocol.h"

#include "chainparams.h"
#include "Logging.h"
#include "utilstrencodings.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

template <typename T1, typename  T2>
static std::map<T2,T1> ReverseMap(const std::map<T1,T2>& forwardMap)
{
    std::map<T2,T1> reversedMap;
    for(const std::pair<T1,T2>& pair: forwardMap)
    {
        reversedMap[pair.second] = pair.first;
    }
    return reversedMap;
}

static const std::map<std::string,int>
inventoryTypeByName = {
    {"tx",MSG_TX},
    {"block",MSG_BLOCK},
    {"filtered block",MSG_FILTERED_BLOCK},
    {"tx lock request",MSG_TXLOCK_REQUEST},
    {"tx lock vote",MSG_TXLOCK_VOTE},
    {"spork",MSG_SPORK},
    {"mn winner",MSG_MASTERNODE_WINNER},
    {"mn scan error",MSG_MASTERNODE_SCANNING_ERROR},
    {"mn budget vote",MSG_BUDGET_VOTE},
    {"mn budget proposal",MSG_BUDGET_PROPOSAL},
    {"mn budget finalized",MSG_BUDGET_FINALIZED},
    {"mn budget finalized vote",MSG_BUDGET_FINALIZED_VOTE},
    {"mn quorum",MSG_MASTERNODE_QUORUM},
    {"mn announce",MSG_MASTERNODE_ANNOUNCE},
    {"mn ping",MSG_MASTERNODE_PING}};
static const std::map<int,std::string> inventoryNameByType = ReverseMap(inventoryTypeByName);

static const int maxInventoryId = (int)inventoryTypeByName.size();

CMessageHeader::CMessageHeader()
{
    memcpy(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    nMessageSize = -1;
    nChecksum = 0;
}

CMessageHeader::CMessageHeader(const char* pszCommand, unsigned int nMessageSizeIn)
{
    memcpy(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    strncpy(pchCommand, pszCommand, COMMAND_SIZE);
    nMessageSize = nMessageSizeIn;
    nChecksum = 0;
}

std::string CMessageHeader::GetCommand() const
{
    return std::string(pchCommand, pchCommand + strnlen_int(pchCommand, COMMAND_SIZE));
}

bool CMessageHeader::IsValid() const
{
    // Check start string
    if (memcmp(pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0)
        return false;

    // Check the command string for errors
    for (const char* p1 = pchCommand; p1 < pchCommand + COMMAND_SIZE; p1++) {
        if (*p1 == 0) {
            // Must be all zeros after the first zero
            for (; p1 < pchCommand + COMMAND_SIZE; p1++)
                if (*p1 != 0)
                    return false;
        } else if (*p1 < ' ' || *p1 > 0x7E)
            return false;
    }

    // Message size
    if (nMessageSize > MAX_SIZE) {
        LogPrintf("CMessageHeader::IsValid() : (%s, %u bytes) nMessageSize > MAX_SIZE\n", GetCommand(), nMessageSize);
        return false;
    }

    return true;
}


CAddress::CAddress() : CService()
{
    Init();
}

CAddress::CAddress(CService ipIn, uint64_t nServicesIn) : CService(ipIn)
{
    Init();
    nServices = nServicesIn;
}

void CAddress::Init()
{
    nServices = NODE_NETWORK;
    nTime = 100000000;
    nLastTry = 0;
}

CInv::CInv()
{
    type = 0;
    hash = 0;
}

CInv::CInv(int typeIn, const uint256& hashIn)
{
    type = (typeIn > maxInventoryId || typeIn < 0)? 0 : typeIn;
    hash = hashIn;
}

CInv::CInv(const std::string& strType, const uint256& hashIn)
{
    if(inventoryTypeByName.count(strType) > 0)
    {
        type = inventoryTypeByName.find(strType)->second;
    }
    else
    {
        type = 0;
        LogPrint("net", "CInv::CInv(string, uint256) : unknown type '%s'", strType);
    }
    hash = hashIn;
}

bool operator<(const CInv& a, const CInv& b)
{
    return (a.type < b.type || (a.type == b.type && a.hash < b.hash));
}

bool CInv::IsKnownType() const
{
    return (type >= 1 && type <= maxInventoryId );
}


const char* CInv::GetCommand() const
{
    static const char* unknownType = "ERROR";
    if (!IsKnownType()){
        LogPrint("net", "CInv::GetCommand() : type=%d unknown type", type);
        return unknownType;
    }
    return inventoryNameByType.find(type)->second.c_str();
}

std::string CInv::ToString() const
{
    return strprintf("%s %s", GetCommand(), hash.ToString());
}

uint256 CInv::GetHash() const
{
    return hash;
}
int CInv::GetType() const
{
    return type;
}