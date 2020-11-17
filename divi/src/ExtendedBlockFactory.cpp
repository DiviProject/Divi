#include <ExtendedBlockFactory.h>

#include <BlockFactory.h>
#include <primitives/transaction.h>
#include <BlockTemplate.h>
#include <sync.h>

ExtendedBlockFactory::ExtendedBlockFactory(
    CWallet& wallet,
    int64_t& lastCoinstakeSearchInterval,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps,
    CChain& chain,
    const CChainParams& chainParameters,
    CTxMemPool& mempool,
    AnnotatedMixin<boost::recursive_mutex>& mainCS
    ): blockFactory_(new BlockFactory(wallet, lastCoinstakeSearchInterval,hashedBlockTimestamps,chain,chainParameters,mempool,mainCS))
    , extraTransactions_()
    , customCoinstake_()
{
}
ExtendedBlockFactory::~ExtendedBlockFactory()
{
    customCoinstake_.reset();
    blockFactory_.reset();
}

CBlockTemplate* ExtendedBlockFactory::CreateNewBlockWithKey(CReserveKey& reserveKey, bool fProofOfStake)
{
    std::unique_ptr<CBlockTemplate> pblocktemplate(blockFactory_->CreateNewBlockWithKey(reserveKey, fProofOfStake));
    CBlock& block = pblocktemplate->block;

    for (const auto& tx : extraTransactions_)
        block.vtx.push_back(*tx);

    if (customCoinstake_ != nullptr) {
        if (!block.IsProofOfStake())
            throw std::runtime_error("trying to set custom coinstake on PoW block");
        assert(block.vtx.size() >= 2);
        CTransaction& coinstake = block.vtx[1];
        assert(coinstake.IsCoinStake());
        coinstake = *customCoinstake_;
    }

    return pblocktemplate.release();
}

/** Adds a transaction to be added in addition to standard mempool
 *  collection for the next block that will be created successfully.  */
void ExtendedBlockFactory::addExtraTransaction(const CTransaction& tx)
{
    extraTransactions_.push_back(std::make_shared<CTransaction>(tx));
}

/** Sets a transaction to use as coinstake on the generated block.
 *  It is up to the caller to ensure that it actually meets the hash
 *  target, e.g. because a large enough coinage and the minimal difficulty
 *  on regtest meet the target always.  */
void ExtendedBlockFactory::setCustomCoinstake(const CTransaction& tx)
{
    customCoinstake_.reset(new CTransaction(tx));
}
