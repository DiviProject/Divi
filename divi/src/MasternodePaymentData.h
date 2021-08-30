#ifndef MASTERNODE_PAYMENT_DATA_H
#define MASTERNODE_PAYMENT_DATA_H
#include <map>
#include <uint256.h>
#include <string>
#include <MasternodePayeeData.h>
#include <sync.h>

class MasternodePaymentData
{
public:
    /** Map from the inventory hashes of mnw's to the corresponding data.  */
    std::map<uint256, CMasternodePaymentWinner> mapMasternodePayeeVotes;
    /** Map from score hashes of blocks to the corresponding winners.  */
    std::map<uint256, CMasternodeBlockPayees> mapMasternodeBlocks;
    mutable CCriticalSection cs_mapMasternodeBlocks;
    mutable CCriticalSection cs_mapMasternodePayeeVotes;

    MasternodePaymentData();
    ~MasternodePaymentData();

    bool winnerIsKnown(const uint256& winnerHash) const;
    bool recordWinner(const CMasternodePaymentWinner& mnw);
    const CMasternodePaymentWinner& getKnownWinner(const uint256& winnerHash) const;
    void pruneOutdatedMasternodeWinners(const int currentChainHeight);
    const CMasternodeBlockPayees* getPayeesForScoreHash(const uint256& hash) const;
    CMasternodeBlockPayees* getPayeesForScoreHash(const uint256& hash);
    const CMasternodePaymentWinner* getPaymentWinnerForHash(const uint256& hash) const;
    CMasternodePaymentWinner* getPaymentWinnerForHash(const uint256& hash);
    bool canVote(const COutPoint& outMasternode, const uint256& scoringBlockHash);

    void CheckAndRemove(){}
    void Clear(){}
    std::string ToString() const;

    ADD_SERIALIZE_METHODS
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapMasternodePayeeVotes);
        READWRITE(mapMasternodeBlocks);
    }
};

#endif //MASTERNODE_PAYMENT_DATA_H