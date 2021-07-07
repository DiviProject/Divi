// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "netfulfilledman.h"
#include <I_Clock.h>

namespace
{

/** Squashes a full network address to the one that we will use as key
 *  inside our cache (which might be just the IP without port).  */
CService SquashAddress(const CService& addr)
{
    /* On regtest, we expect different peers to be on the same IP
       address (localhost), and that is fine.  */
    if (Params().NetworkID() == CBaseChainParams::REGTEST)
        return addr;

    return CService(addr, 0);
}

} // anonymous namespace

class FixedTimeClock final: public I_Clock
{
public:
    virtual int64_t getTime() const
    {
        return 0;
    }
};
static FixedTimeClock dummyClock;

CNetFulfilledRequestManager::CNetFulfilledRequestManager(): clock_(dummyClock)
{
}

CNetFulfilledRequestManager::CNetFulfilledRequestManager(
    const I_Clock& clock
    ):clock_(clock)
{
}

void CNetFulfilledRequestManager::AddFulfilledRequest(const CService& addr, const std::string& strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    mapFulfilledRequests[addrSquashed][strRequest] = clock_.getTime() + Params().FulfilledRequestExpireTime();
}

bool CNetFulfilledRequestManager::HasFulfilledRequest(const CService& addr, const std::string& strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.find(addrSquashed);

    return  it != mapFulfilledRequests.end() &&
            it->second.find(strRequest) != it->second.end() &&
            it->second[strRequest] > clock_.getTime();
}

void CNetFulfilledRequestManager::AddPendingRequest(const CService& addr, const std::string& strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    pendingRequests[addrSquashed][strRequest] = clock_.getTime() + Params().FulfilledRequestExpireTime();
}
void CNetFulfilledRequestManager::FulfillPendingRequest(const CService& addr, const std::string& strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    fulfilledreqmap_t::iterator it = pendingRequests.find(addrSquashed);

    if (it != pendingRequests.end()) {
        AddFulfilledRequest(addr,strRequest);
        it->second.erase(strRequest);
    }
}
bool CNetFulfilledRequestManager::HasPendingRequest(const CService& addr, const std::string& strRequest) const
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    fulfilledreqmap_t::const_iterator it = pendingRequests.find(addrSquashed);

    if(it == pendingRequests.end())
    {
        return false;
    }
    fulfilledreqmapentry_t::const_iterator itToRequest = it->second.find(strRequest);
    if (itToRequest == it->second.end())
    {
        return false;
    }
    if(clock_.getTime() <= itToRequest->second)
    {
        return true;
    }
    else
    {
        return false;
    }
}

void CNetFulfilledRequestManager::RemoveFulfilledRequest(const CService& addr, const std::string& strRequest)
{
    LOCK(cs_mapFulfilledRequests);
    const auto addrSquashed = SquashAddress(addr);
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.find(addrSquashed);

    if (it != mapFulfilledRequests.end()) {
        it->second.erase(strRequest);
    }
}

void CNetFulfilledRequestManager::CheckAndRemove()
{
    LOCK(cs_mapFulfilledRequests);

    int64_t now = clock_.getTime();
    fulfilledreqmap_t::iterator it = mapFulfilledRequests.begin();

    while(it != mapFulfilledRequests.end()) {
        fulfilledreqmapentry_t::iterator it_entry = it->second.begin();
        while(it_entry != it->second.end()) {
            if(now > it_entry->second) {
                it->second.erase(it_entry++);
            } else {
                ++it_entry;
            }
        }
        if(it->second.size() == 0) {
            mapFulfilledRequests.erase(it++);
        } else {
            ++it;
        }
    }
}

void CNetFulfilledRequestManager::Clear()
{
    LOCK(cs_mapFulfilledRequests);
    mapFulfilledRequests.clear();
}

std::string CNetFulfilledRequestManager::ToString() const
{
    std::ostringstream info;
    info << "Nodes with fulfilled requests: " << (int)mapFulfilledRequests.size();
    return info.str();
}
