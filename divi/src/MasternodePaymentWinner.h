#ifndef MASTERNODE_PAYMENT_WINNER_H
#define MASTERNODE_PAYMENT_WINNER_H
#include <primitives/transaction.h>
#include <string>
// for storing the winning payments
class CMasternodePaymentWinner
{
public:
    CTxIn vinMasternode;

private:
    /* Masternode payment blocks are uniquely identified by the scoring hash used
       for their scoring computation.  This scoring hash is based on the block
       height, but one block height might have multiple hashes in case of a
       reorg (so the hash is the more robust identifier).  We use the block
       height for messages sent on the wire because that's what the protocol
       is, but translate them to the scoring hash and use the seed has instead
       for internal storage and processing.  The block height is also used
       to check freshness, e.g. when only accepting payment messages for
       somewhat recent blocks.  */
    uint256 scoringBlockHash;
    int nBlockHeight;

public:
    const uint256& getScoringBlockHash() const
    {
        return scoringBlockHash;
    }

    CScript payee;
    std::vector<unsigned char> signature;

    CMasternodePaymentWinner();

    explicit CMasternodePaymentWinner(const CTxIn& vinIn, const int height, const uint256& hash);

    uint256 GetHash() const;

    std::string getMessageToSign() const;
    void Relay() const;

    void AddPayee(const CScript& payeeIn);

    inline int GetHeight() const
    {
        return nBlockHeight;
    }

    const uint256& GetScoreHash() const;

    /** Computes the score hash from our height.  This has to be called
     *  explicitly after deserialising and before GetScoreHash() can
     *  be used.  Returns false if the hash could not be found.  */
    bool ComputeScoreHash();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(vinMasternode);
        READWRITE(nBlockHeight);
        READWRITE(payee);
        READWRITE(signature);

        if (nType == SER_DISK) {
            /* For saving in the on-disk cache files, we include the
               scoring hash as well.  */
            READWRITE(scoringBlockHash);
        } else if (ser_action.ForRead()) {
            /* After parsing from network, the scoringBlockHash field is not set
               and must not be accessed (e.g. through GetScoreHash) until
               ComputeScoreHash() has been called explicitly in a place
               that is convenient.  We do this (rather than computing here
               right away) to prevent potential DoS vectors where we might
               want to perform some more validation before doing the
               expensive computation.  */
            scoringBlockHash.SetNull();
        }
    }

    std::string ToString() const;
};
#endif// MASTERNODE_PAYMENT_WINNER_H