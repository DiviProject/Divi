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
    int chainTipHeight;
    MasternodePaymentData& paymentData_;
    CMasternodeMan& masternodeManager_;

    /** Map from the inventory hashes of mnw's to the corresponding data.  */
    std::map<uint256, CMasternodePaymentWinner>& mapMasternodePayeeVotes;
    /** Map from score hashes of blocks to the corresponding winners.  */
    std::map<uint256, CMasternodeBlockPayees>& mapMasternodeBlocks;

    mutable CCriticalSection cs_mapMasternodeBlocks;
    mutable CCriticalSection cs_mapMasternodePayeeVotes;

    bool GetBlockPayee(const uint256& seedHash, CScript& payee) const;
    bool CheckMasternodeWinnerSignature(const CMasternodePaymentWinner& winner) const;
    bool CheckMasternodeWinnerValidity(const CMasternodeSync& masternodeSynchronization,const CMasternodePaymentWinner& winner, CNode* pnode, std::string& strError) const;
public:
    static const int MNPAYMENTS_SIGNATURES_REQUIRED;
    static const int MNPAYMENTS_SIGNATURES_TOTAL;
    void updateChainTipHeight(const CBlockIndex* pindex);

    CMasternodePayments(MasternodePaymentData& paymentData, CMasternodeMan& masternodeManager);
    ~CMasternodePayments();

    bool AddWinningMasternode(const CMasternodePaymentWinner &winner);

    void Sync(CNode* node, int nCountNeeded);
    void CheckAndRemove();
    void PruneOldMasternodeWinnerData(CMasternodeSync& masternodeSynchronization);

    bool IsTransactionValid(const I_BlockSubsidyProvider& subsidies,const CTransaction& txNew, const uint256& seedHash) const;
    bool IsScheduled(const CScript mnpayee, int nNotBlockHeight) const;

    bool CanVote(const COutPoint& outMasternode, const uint256& seedHash);

    int GetMinMasternodePaymentsProto() const;
    void ProcessMessageMasternodePayments(CMasternodeSync& masternodeSynchronization,CNode* pfrom, const std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(const uint256& seedHash) const;
    void FillBlockPayee(const CBlockIndex* pindexPrev, CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake) const;
    std::string ToString() const;

    unsigned FindLastPayeePaymentTime(const CMasternode& masternode, const unsigned maxBlockDepth) const;
    CScript GetNextMasternodePayeeInQueueForPayment(const CBlockIndex* pindex, const int offset) const;
    std::vector<CMasternode*> GetMasternodePaymentQueue(const CBlockIndex* pindex, int offset) const;
    std::vector<CMasternode*> GetMasternodePaymentQueue(const uint256& seedHash, const int nBlockHeight) const;

    /** Returns the given masternode's rank among all active and with the
     *  given minimum protocol version.  Returns (unsigned)-1 if the node is not
     *  found or not active itself.
     *
     *  If the given node is not in the top-"nCheckNum" masternodes by rank, then
     *  nCheckNum + 1 is returned (instead of the exact rank).  */
    unsigned GetMasternodeRank(const CTxIn& vin, const uint256& seedHash,
                               int minProtocol, unsigned nCheckNum) const;
    void ResetRankingCache();

    /** Retrieves the payment winner for the given hash.  Returns null
     *  if there is no entry for that hash.  */
    const CMasternodePaymentWinner* GetPaymentWinnerForHash(const uint256& hash) const {
        return const_cast<CMasternodePayments*>(this)->GetPaymentWinnerForHash(hash);
    }
    CMasternodePaymentWinner* GetPaymentWinnerForHash(const uint256& hash) {
        const auto mit = mapMasternodePayeeVotes.find(hash);
        if (mit == mapMasternodePayeeVotes.end())
            return nullptr;
        return &mit->second;
    }

    /** Retrieves the payees for the given block.  Returns null if there is
     *  no matching entry.  */
    const CMasternodeBlockPayees* GetPayeesForScoreHash(const uint256& hash) const {
        return const_cast<CMasternodePayments*>(this)->GetPayeesForScoreHash(hash);
    }
    CMasternodeBlockPayees* GetPayeesForScoreHash(const uint256& hash) {
        const auto mit = mapMasternodeBlocks.find(hash);
        if (mit == mapMasternodeBlocks.end())
            return nullptr;
        return &mit->second;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapMasternodePayeeVotes);
        READWRITE(mapMasternodeBlocks);
    }
};


#endif
