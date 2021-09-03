#include <ExtendedBlockFactory.h>

#include <BlockFactory.h>
#include <primitives/transaction.h>
#include <BlockTemplate.h>
#include <I_BlockTransactionCollector.h>
#include <I_PoSTransactionCreator.h>
#include <sync.h>

class ExtendedBlockTransactionCollector final: public I_BlockTransactionCollector
{
public:
    const std::vector<std::shared_ptr<CTransaction>>& extraTransactions_;
    const std::unique_ptr<CTransaction>& customCoinstake_;
    const bool& ignoreMempool_;
    I_BlockTransactionCollector& decoratedTransactionCollector_;
public:
    ExtendedBlockTransactionCollector(
        const std::vector<std::shared_ptr<CTransaction>>& extraTransactions,
        const std::unique_ptr<CTransaction>& customCoinstake,
        const bool& ignoreMempool,
        I_BlockTransactionCollector& decoratedTransactionCollector
        ): extraTransactions_(extraTransactions)
        , customCoinstake_(customCoinstake)
        , ignoreMempool_(ignoreMempool)
        , decoratedTransactionCollector_(decoratedTransactionCollector)
    {
    }

    bool CollectTransactionsIntoBlock(CBlockTemplate& pblocktemplate) const override
    {
        if(!decoratedTransactionCollector_.CollectTransactionsIntoBlock(pblocktemplate))
        {
            return false;
        }

        CBlock& block = pblocktemplate.block;
        if (ignoreMempool_) {
            const unsigned targetSize = (block.IsProofOfStake() ? 2 : 1);
            assert(block.vtx.size() >= targetSize);
            block.vtx.resize(targetSize);
        }

        for (const auto& tx : extraTransactions_)
            block.vtx.push_back(*tx);

        if (customCoinstake_ != nullptr)
        {
            if (!block.IsProofOfStake())
                throw std::runtime_error("trying to set custom coinstake on PoW block");
            assert(block.vtx.size() >= 2);
            CTransaction& coinstake = block.vtx[1];
            assert(coinstake.IsCoinStake());
            coinstake = *customCoinstake_;
        }
        return true;
    }
};

class ExtendedPoSTransactionCreator final: public I_PoSTransactionCreator
{
private:
    const std::unique_ptr<CTransaction>& customCoinstake_;
    I_PoSTransactionCreator& transactionCreator_;
public:
    ExtendedPoSTransactionCreator(
        const std::unique_ptr<CTransaction>& customCoinstake,
        I_PoSTransactionCreator& transactionCreator
        ): customCoinstake_(customCoinstake)
        , transactionCreator_(transactionCreator)
    {
    }

    bool CreateProofOfStake(
        const CBlockIndex* chainTip,
        uint32_t blockBits,
        CMutableTransaction& txCoinStake,
        unsigned int& nTxNewTime) override
    {
        if (customCoinstake_ != nullptr)
        {
            if (!customCoinstake_->IsCoinStake())
                throw std::runtime_error("trying to use non-coinstake to set custom coinstake on PoW block");
            txCoinStake = CMutableTransaction(*customCoinstake_);
            return true;
        }
        else
        {
            return transactionCreator_.CreateProofOfStake(chainTip,blockBits,txCoinStake,nTxNewTime);
        }
    }
};

ExtendedBlockFactory::ExtendedBlockFactory(
    const I_BlockSubsidyProvider& blockSubsidies,
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    const Settings& settings,
    const CChain& chain,
    const CChainParams& chainParameters
    ): extraTransactions_()
    , customCoinstake_()
    , ignoreMempool_(false)
    , extendedTransactionCollector_(new ExtendedBlockTransactionCollector(extraTransactions_,customCoinstake_,ignoreMempool_,blockTransactionCollector))
    , blockFactory_(new BlockFactory(blockSubsidies,*extendedTransactionCollector_,coinstakeCreator, settings, chain,chainParameters))
{
}
ExtendedBlockFactory::~ExtendedBlockFactory()
{
    customCoinstake_.reset();
    blockFactory_.reset();
}

CBlockTemplate* ExtendedBlockFactory::CreateNewBlockWithKey(CReserveKey& reserveKey, bool fProofOfStake)
{
    return blockFactory_->CreateNewBlockWithKey(reserveKey, fProofOfStake);
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

void ExtendedBlockFactory::setIgnoreMempool(const bool val)
{
    ignoreMempool_ = val;
}
