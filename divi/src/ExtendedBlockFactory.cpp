#include <ExtendedBlockFactory.h>

#include <BlockFactory.h>
#include <primitives/transaction.h>
#include <BlockTemplate.h>
#include <I_BlockTransactionCollector.h>
#include <I_PoSTransactionCreator.h>
#include <sync.h>
#include <BlockSigning.h>
#include <I_StakingCoinSelector.h>

class ExtendedBlockTransactionCollector final: public I_BlockTransactionCollector
{
public:
    const std::vector<std::shared_ptr<CTransaction>>& extraTransactions_;
    const bool& ignoreMempool_;
    I_BlockTransactionCollector& decoratedTransactionCollector_;
public:
    ExtendedBlockTransactionCollector(
        const std::vector<std::shared_ptr<CTransaction>>& extraTransactions,
        const bool& ignoreMempool,
        I_BlockTransactionCollector& decoratedTransactionCollector
        ): extraTransactions_(extraTransactions)
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

        return true;
    }
};

class ExtendedPoSTransactionCreator final: public I_PoSTransactionCreator
{
private:
    const I_StakingWallet& wallet_;
    const std::unique_ptr<CTransaction>& customCoinstake_;
    I_PoSTransactionCreator& transactionCreator_;
public:
    ExtendedPoSTransactionCreator(
        const I_StakingWallet& wallet,
        const std::unique_ptr<CTransaction>& customCoinstake,
        I_PoSTransactionCreator& transactionCreator
        ): wallet_(wallet)
        , customCoinstake_(customCoinstake)
        , transactionCreator_(transactionCreator)
    {
    }

    bool CreateProofOfStake(
        const CBlockIndex* chainTip,
        CBlock& block) const override
    {
        bool coinstakeCreated = transactionCreator_.CreateProofOfStake(chainTip,block);
        if (customCoinstake_ != nullptr)
        {
            if (!customCoinstake_->IsCoinStake())
                throw std::runtime_error("trying to use non-coinstake to set custom coinstake in block");
            block.vtx[1] = CMutableTransaction(*customCoinstake_);
            block.hashMerkleRoot = block.BuildMerkleTree();
            return SignBlock(wallet_,block);
        }
        else
        {
            return coinstakeCreated;
        }
    }
};

ExtendedBlockFactory::ExtendedBlockFactory(
    const I_StakingWallet& wallet,
    const I_BlockSubsidyProvider& blockSubsidies,
    const I_DifficultyAdjuster& difficultyAdjuster,
    I_BlockTransactionCollector& blockTransactionCollector,
    I_PoSTransactionCreator& coinstakeCreator,
    const Settings& settings,
    const CChain& chain,
    const CChainParams& chainParameters
    ): extraTransactions_()
    , customCoinstake_()
    , ignoreMempool_(false)
    , extendedTransactionCollector_(new ExtendedBlockTransactionCollector(extraTransactions_,ignoreMempool_,blockTransactionCollector))
    , extendedCoinstakeCreator_(new ExtendedPoSTransactionCreator(wallet,customCoinstake_,coinstakeCreator))
    , blockFactory_(new BlockFactory(blockSubsidies,difficultyAdjuster,*extendedTransactionCollector_,*extendedCoinstakeCreator_, settings, chain,chainParameters))
{
}
ExtendedBlockFactory::~ExtendedBlockFactory()
{
    customCoinstake_.reset();
    blockFactory_.reset();
}

void ExtendedBlockFactory::VerifyBlockWithIsCompatibleWithCustomCoinstake(const CBlock& block)
{
    if (customCoinstake_ != nullptr)
    {
        if (!block.IsProofOfStake())
            throw std::runtime_error("trying to set custom coinstake on PoW block");
        assert(block.vtx.size() >= 2 && "PoS blocks must have at least 2 transactions");
        assert(block.vtx[1].IsCoinStake() && "PoS blocks' second transaction must be a coinstake");
    }
}

CBlockTemplate* ExtendedBlockFactory::CreateNewPoWBlock(const CScript& scriptPubKey)
{
    return blockFactory_->CreateNewPoWBlock(scriptPubKey);
}
CBlockTemplate* ExtendedBlockFactory::CreateNewPoSBlock()
{
    CBlockTemplate* blockTemplate = blockFactory_->CreateNewPoSBlock();
    VerifyBlockWithIsCompatibleWithCustomCoinstake(blockTemplate->block);
    return blockTemplate;
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

void ExtendedBlockFactory::reset()
{
    extraTransactions_.clear();
    customCoinstake_.reset();
    ignoreMempool_ = false;
}