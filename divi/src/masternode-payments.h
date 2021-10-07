// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include <BlockRewards.h>
#include <key.h>
#include <boost/lexical_cast.hpp>
#include <primitives/transaction.h>
#include <sync.h>
#include <MasternodePayeeData.h>

class CBlock;
class CMasternodePayments;
class CMasternodePaymentWinner;
class CMasternodeBlockPayees;
class CMasternode;
class CNode;
class CBlockIndex;
class CDataStream;
class CMasternodeSync;
class I_BlockSubsidyProvider;
class CMasternodeMan;
class MasternodePaymentData;
class MasternodeNetworkMessageManager;
class CChain;
class CNetFulfilledRequestManager;
class CNode;
//
// Masternode Payments Class
// Keeps track of who should get paid for which blocks
//

class CMasternodePayments
{
private:
    // Cache of the most recent masternode ranks, so we can efficiently check
    // if some masternode is in the top-20 for a recent block height.
    class RankingCache;
    std::unique_ptr<RankingCache> rankingCache;

    int nSyncedFromPeer;
    int nLastBlockHeight;
    CNetFulfilledRequestManager& networkFulfilledRequestManager_;
    MasternodePaymentData& paymentData_;
    MasternodeNetworkMessageManager& networkMessageManager_;
    CMasternodeMan& masternodeManager_;
    CMasternodeSync& masternodeSynchronization_;
    const CChain& activeChain_;

    /** Map from score hashes of blocks to the corresponding winners.  */
    std::map<uint256, CMasternodeBlockPayees>& mapMasternodeBlocks;
    CCriticalSection& cs_mapMasternodeBlocks;

    bool GetBlockPayee(const uint256& scoringBlockHash, CScript& payee) const;
    bool CheckMasternodeWinnerSignature(const CMasternodePaymentWinner& winner,const CPubKey& mnPubKey) const;
    bool CheckMasternodeWinnerValidity(const CMasternodePaymentWinner& winner, CMasternode& masternode) const;
    bool IsTransactionValid(const CMasternodeBlockPayees& payees, const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew) const;
    bool CheckMasternodeWinnerCandidate(CNode* pfrom, CMasternodePaymentWinner& winner) const;
public:
    static const int MNPAYMENTS_SIGNATURES_REQUIRED;
    static const int MNPAYMENTS_SIGNATURES_TOTAL;

    CMasternodePayments(
        CNetFulfilledRequestManager& networkFulfilledRequestManager,
        MasternodePaymentData& paymentData,
        MasternodeNetworkMessageManager& networkMessageManager,
        CMasternodeMan& masternodeManager,
        CMasternodeSync& masternodeSynchronization,
        const CChain& activeChain);
    ~CMasternodePayments();

    bool AddWinningMasternode(const CMasternodePaymentWinner &winner);

    void PruneOldMasternodeWinnerData();

    bool IsTransactionValid(const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew, const uint256& scoringBlockHash) const;
    bool IsScheduled(const CScript mnpayee, int nNotBlockHeight) const;

    bool CanVote(const COutPoint& outMasternode, const uint256& scoringBlockHash) const;

    void ProcessMasternodeWinners(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    void FillBlockPayee(const CBlockIndex* pindexPrev, CMutableTransaction& txNew, const CBlockRewards &rewards) const;

    CScript GetNextMasternodePayeeInQueueForPayment(const CBlockIndex* pindex, const int offset) const;
    std::vector<CMasternode*> GetMasternodePaymentQueue(const CBlockIndex* pindex, int offset) const;
    std::vector<CMasternode*> GetMasternodePaymentQueue(const uint256& scoringBlockHash, const int nBlockHeight) const;

    /** Returns the given masternode's rank among all active and with the
     *  given minimum protocol version.  Returns (unsigned)-1 if the node is not
     *  found or not active itself.
     *
     *  If the given node is not in the top-"nCheckNum" masternodes by rank, then
     *  nCheckNum + 1 is returned (instead of the exact rank).  */
    unsigned GetMasternodeRank(
        const CTxIn& vin,
        const uint256& scoringBlockHash,
        int minProtocol,
        unsigned nCheckNum) const;
    void ResetRankingCache();
};


#endif
