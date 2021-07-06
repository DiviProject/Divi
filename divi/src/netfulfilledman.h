// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef NETFULFILLEDMAN_H
#define NETFULFILLEDMAN_H

#include "netbase.h"
#include "serialize.h"
#include "sync.h"
class I_Clock;

// Fulfilled requests are used to prevent nodes from asking for the same data on sync
// and from being banned for doing so too often.
class CNetFulfilledRequestManager
{
private:
    typedef std::map<std::string, int64_t> fulfilledreqmapentry_t;
    typedef std::map<CService, fulfilledreqmapentry_t> fulfilledreqmap_t;

    //keep track of what node has/was asked for and when
    fulfilledreqmap_t mapFulfilledRequests;
    fulfilledreqmap_t pendingRequests;
    mutable CCriticalSection cs_mapFulfilledRequests;
    const I_Clock& clock_;

public:
    void RemoveFulfilledRequest(const CService& addr, const std::string& strRequest);
    CNetFulfilledRequestManager();
    CNetFulfilledRequestManager(const I_Clock& clock);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs_mapFulfilledRequests);
        READWRITE(mapFulfilledRequests);
    }

    void AddFulfilledRequest(const CService& addr, const std::string& strRequest);
    bool HasFulfilledRequest(const CService& addr, const std::string& strRequest);
    void AddPendingRequest(const CService& addr, const std::string& strRequest);
    void FulfillPendingRequest(const CService& addr, const std::string& strRequest);
    bool HasPendingRequest(const CService& addr, const std::string& strRequest) const;

    void CheckAndRemove();
    void Clear();

    std::string ToString() const;
};

#endif
