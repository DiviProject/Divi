#include <PoSTransactionCreator.h>

#include <I_StakingCoinSelector.h>
#include <I_ProofOfStakeGenerator.h>
#include <masternode-payments.h>
#include <script/sign.h>
#include <utilmoneystr.h>
#include <I_BlockIncentivesPopulator.h>
#include <I_BlockSubsidyProvider.h>
#include <Settings.h>
#include <blockmap.h>
#include <chain.h>
#include <chainparams.h>
#include <StakingData.h>
#include <I_PoSStakeModifierService.h>
#include <Logging.h>
#include <utiltime.h>
#include <StakableCoin.h>
#include <timedata.h>
#include <ForkActivation.h>
#include <BlockSigning.h>

class StakedCoins
{
private:
    std::set<StakableCoin> underlyingSet_;
    std::vector<const StakableCoin*> shuffledSet_;
    int64_t timestampOfLastUpdate_;
    bool utxoPermutationEnabled_;
public:
    void resetCoins()
    {
        shuffledSet_.clear();
        underlyingSet_.clear();
    }
    StakedCoins(
        bool utxoPermutationEnabled
        ): underlyingSet_()
        , shuffledSet_()
        , timestampOfLastUpdate_(0)
        , utxoPermutationEnabled_(utxoPermutationEnabled)
    {
    }
    ~StakedCoins()
    {
        resetCoins();
    }
    StakedCoins(const StakedCoins& other) = delete;
    StakedCoins& operator=(const StakedCoins& other) = delete;

    const int64_t& timestamp() const
    {
        return timestampOfLastUpdate_;
    }
    void updateTimestamp()
    {
        timestampOfLastUpdate_ = GetTime();
    }
    void resetTimestamp()
    {
        timestampOfLastUpdate_ = 0;
    }
    void updateShuffledSet()
    {
        shuffledSet_.clear();
        shuffledSet_.reserve(underlyingSet_.size());
        for(const StakableCoin& coin: underlyingSet_)
        {
            shuffledSet_.push_back(&coin);
        }
        if(utxoPermutationEnabled_) std::random_shuffle(shuffledSet_.begin(), shuffledSet_.end());
    }
    const std::vector<const StakableCoin*>& getShuffledSet() const
    {
        return shuffledSet_;
    }
    std::set<StakableCoin>& asSet()
    {
        return underlyingSet_;
    }
};

PoSTransactionCreator::PoSTransactionCreator(
    const Settings& settings,
    const CChainParams& chainParameters,
    const CChain& activeChain,
    const BlockMap& mapBlockIndex,
    const I_BlockSubsidyProvider& blockSubsidies,
    const I_BlockIncentivesPopulator& incentives,
    const I_ProofOfStakeGenerator& proofGenerator,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps
    ): settings_(settings)
    , chainParameters_(chainParameters)
    , activeChain_(activeChain)
    , blockIndexByHash_(mapBlockIndex)
    , blockSubsidies_( blockSubsidies )
    , incentives_(incentives)
    , proofGenerator_(proofGenerator )
    , stakedCoins_(new StakedCoins(settings_.GetBoolArg("-vault", false)))
    , wallet_()
    , hashedBlockTimestamps_(hashedBlockTimestamps)
    , hashproofTimestampMinimumValue_(0)
{
}

PoSTransactionCreator::~PoSTransactionCreator()
{
    wallet_.reset();
    stakedCoins_.reset();
}

bool PoSTransactionCreator::SelectCoins() const
{
    if(wallet_ == nullptr) return false;
    if (chainParameters_.NetworkID() == CBaseChainParams::REGTEST ||
        GetTime() - stakedCoins_->timestamp() > settings_.GetArg("-stakeupdatetime",300))
    {
        stakedCoins_->resetCoins();
        if (!wallet_->SelectStakeCoins(stakedCoins_->asSet())) {
            return error("failed to select coins for staking");
        }
        stakedCoins_->updateShuffledSet();
        stakedCoins_->updateTimestamp();
    }

    if (stakedCoins_->asSet().empty()) {
        return error("no coins available for staking");
    }

    return true;
}

void MarkTransactionAsCoinstake(CMutableTransaction& txNew)
{
    txNew.vin.clear();
    txNew.vout.clear();
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));
}

bool IsSupportedScript(
    const CScript& scriptPubKeyOut,
    bool& isVaultScript)
{
    txnouttype whichType;
    std::vector<valtype> vSolutions;
    if (!ExtractScriptPubKeyFormat(scriptPubKeyOut, whichType, vSolutions)) {
        LogPrintf("CreateCoinStake : failed to parse kernel\n");
        return false;
    }
    LogPrint("minting","%s : parsed kernel type=%d\n",__func__, whichType);
    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH && whichType != TX_VAULT)
    {
        LogPrint("minting","%s : no support for kernel type=%d\n",__func__, whichType);
        return false; // only support pay to public key and pay to address
    }
    isVaultScript =whichType == TX_VAULT;
    return true;
}

bool PoSTransactionCreator::SetSuportedStakingScript(
    const StakableCoin& stakableCoin,
    CMutableTransaction& txNew) const
{
    txNew.vin.emplace_back(stakableCoin.utxo);
    txNew.vout.emplace_back(0, stakableCoin.GetTxOut().scriptPubKey);

    return true;
}

void PoSTransactionCreator::CombineUtxos(
    const CAmount& stakeSplit,
    CMutableTransaction& txNew,
    CAmount& nCredit,
    std::vector<const CTransaction*>& walletTransactions) const
{
    static const unsigned maxInputs = settings_.MaxNumberOfPoSCombinableInputs();
    const CAmount nCombineThreshold = stakeSplit / 2;
    std::vector<StakableCoin> vCombineCandidates;
    for(const StakableCoin& pcoin : stakedCoins_->asSet())
    {
        if(pcoin.GetTxOut().scriptPubKey == txNew.vout[1].scriptPubKey &&
            pcoin.tx->GetHash() != txNew.vin[0].prevout.hash)
        {
            if(pcoin.GetTxOut().nValue + nCredit > nCombineThreshold)
                continue;

            vCombineCandidates.push_back(pcoin);
        }
    }

    std::sort(std::begin(vCombineCandidates), std::end(vCombineCandidates),
        [](const StakableCoin &left, const StakableCoin &right) {
            return left.GetTxOut().nValue < right.GetTxOut().nValue;
        });

    for(const auto& pcoin : vCombineCandidates)
    {
        if (txNew.vin.size() >= maxInputs ||
            nCredit > nCombineThreshold ||
            nCredit + pcoin.GetTxOut().nValue > nCombineThreshold)
            break;

        txNew.vin.emplace_back(pcoin.utxo);
        nCredit += pcoin.GetTxOut().nValue;
        walletTransactions.push_back(pcoin.tx);
    }
}

bool PoSTransactionCreator::FindHashproof(
    const CBlockIndex* chainTip,
    unsigned int nBits,
    unsigned int& nTxNewTime,
    const StakableCoin& stakeData,
    CMutableTransaction& txNew) const
{
    BlockMap::const_iterator it = blockIndexByHash_.find(stakeData.blockHashOfFirstConfirmation);
    if (it == blockIndexByHash_.end())
    {
        LogPrint("minting","%s failed to find block index for %s\n",__func__,stakeData.blockHashOfFirstConfirmation);
        return false;
    }

    StakingData stakingData(
        nBits,
        static_cast<unsigned>(it->second->GetBlockTime()),
        it->second->GetBlockHash(),
        stakeData.utxo,
        stakeData.GetTxOut().nValue,
        chainTip->GetBlockHash());
    HashproofCreationResult hashproofResult = proofGenerator_.createHashproofTimestamp(stakingData,nTxNewTime);
    if(!hashproofResult.failedAtSetup())
    {
        hashedBlockTimestamps_.clear();
        hashedBlockTimestamps_[chainTip->nHeight] = GetTime();
    }
    if (hashproofResult.succeeded())
    {
        if (hashproofResult.timestamp() <= chainTip->GetMedianTimePast())
        {
            LogPrint("minting","%s : kernel found, but it is too far in the past \n",__func__);
            return false;
        }
        LogPrint("minting","%s : kernel found for %s\n",__func__, stakeData.tx->ToStringShort());

        SetSuportedStakingScript(stakeData,txNew);
        nTxNewTime = hashproofResult.timestamp();
        return true;
    }
    return false;
}

const StakableCoin* PoSTransactionCreator::FindProofOfStake(
    const CBlockIndex* chainTip,
    uint32_t blockBits,
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime,
    bool& isVaultScript) const
{
    for (const StakableCoin* const pcoin: stakedCoins_->getShuffledSet())
    {
        if(!IsSupportedScript(pcoin->GetTxOut().scriptPubKey,isVaultScript))
        {
            continue;
        }
        if(chainTip->nHeight != activeChain_.Height())
        {
            hashproofTimestampMinimumValue_ = 0;
            return nullptr;
        }
        if(FindHashproof(chainTip,blockBits, nTxNewTime, *pcoin,txCoinStake) )
        {
            return pcoin;
        }
    }
    hashproofTimestampMinimumValue_ = nTxNewTime;
    return nullptr;
}

void PoSTransactionCreator::SplitOrCombineUTXOS(
    const CAmount stakeSplit,
    const CBlockIndex* chainTip,
    CMutableTransaction& txCoinStake,
    const StakableCoin& stakeData,
    std::vector<const CTransaction*>& vwtxPrev) const
{
    CBlockRewards blockSubdidy = blockSubsidies_.GetBlockSubsidity(chainTip->nHeight + 1);
    CAmount nCredit = stakeData.GetTxOut().nValue + blockSubdidy.nStakeReward + (ActivationState(chainTip).IsActive(Fork::DeprecateMasternodes)? blockSubdidy.nMasternodeReward : 0);
    constexpr char autocombineSettingLookup[] = "-autocombine";
    bool autocombine = settings_.GetBoolArg(autocombineSettingLookup,true);
    if (nCredit > stakeSplit )
    {
        txCoinStake.vout.push_back(txCoinStake.vout.back());
        txCoinStake.vout[1].nValue = nCredit / 2;
        txCoinStake.vout[2].nValue = nCredit - txCoinStake.vout[1].nValue;
    }
    else if(autocombine)
    {
        CombineUtxos(stakeSplit, txCoinStake,nCredit,vwtxPrev);
        txCoinStake.vout[1].nValue = nCredit;
    }
    else
    {
        txCoinStake.vout[1].nValue = nCredit;
    }

}

void PoSTransactionCreator::AppendBlockRewardPayoutsToTransaction(
    const CBlockIndex* chainTip,
    CMutableTransaction& txCoinStake) const
{
    CBlockRewards blockSubdidy = blockSubsidies_.GetBlockSubsidity(chainTip->nHeight + 1);
    incentives_.FillBlockPayee(txCoinStake,blockSubdidy,chainTip);
}

bool PoSTransactionCreator::attachBlockProof(
    const CBlockIndex* chainTip,
    CBlock& block) const
{
    if(wallet_ == nullptr) return false;
    CMutableTransaction txCoinStake;
    unsigned int nTxNewTime = block.nTime;

    uint32_t blockBits = block.nBits;
    MarkTransactionAsCoinstake(txCoinStake);

    if(!SelectCoins()) return false;

    if(hashedBlockTimestamps_.count(chainTip->nHeight) == 0u)
    {
        hashproofTimestampMinimumValue_ = 0;
    }
    int64_t adjustedTime = GetAdjustedTime();
    int64_t minimumTime = chainTip->GetMedianTimePast() + 1;
    const int64_t maximumTime = adjustedTime + settings_.MaxFutureBlockDrift() - 1;
    minimumTime += chainParameters_.RetargetDifficulty()? I_ProofOfStakeGenerator::nHashDrift: 0;
    minimumTime = std::max(hashproofTimestampMinimumValue_,minimumTime);
    if(maximumTime <= minimumTime) return false;
    nTxNewTime = std::min(std::max(adjustedTime, minimumTime), maximumTime);

    bool isVaultScript = false;
    const StakableCoin* successfullyStakableUTXO =
        FindProofOfStake(chainTip, blockBits,txCoinStake,nTxNewTime,isVaultScript);
    if( successfullyStakableUTXO == nullptr)
    {
        return false;
    }
    CAmount nCredit = successfullyStakableUTXO->GetTxOut().nValue;
    if(nCredit == 0)
    {
        return false;
    }

    std::vector<const CTransaction*> vwtxPrev(1, successfullyStakableUTXO->tx);

    constexpr char stakeSplitSettingLookup[] = "-stakesplitthreshold";
    CAmount stakeSplit = static_cast<CAmount>(settings_.GetArg(stakeSplitSettingLookup,20000)* COIN);
    if(isVaultScript)
    {
        stakeSplit = std::max(stakeSplit,20000*COIN);
    }

    SplitOrCombineUTXOS(stakeSplit,chainTip,txCoinStake,*successfullyStakableUTXO,vwtxPrev);
    AppendBlockRewardPayoutsToTransaction(chainTip,txCoinStake);

    int nIn = 0;
    for (const CTransaction* pcoin : vwtxPrev) {
        if (!SignSignature(*wallet_, *pcoin, txCoinStake, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    block.nTime = nTxNewTime;
    block.vtx[1] = txCoinStake;

    block.hashMerkleRoot = block.BuildMerkleTree();
    LogPrintf("%s: proof-of-stake block found %s \n",__func__, block.GetHash());
    if (!SignBlock(*wallet_, block)) {
        LogPrintf("%s: Signing new block failed \n",__func__);
        return false;
    }
    LogPrintf("%s: proof-of-stake block was signed %s \n", __func__, block.GetHash());

    stakedCoins_->resetTimestamp(); //this will trigger stake set to repopulate next round
    return true;
}

void PoSTransactionCreator::setWallet(I_StakingWallet& wallet)
{
    wallet_.reset(&wallet);
    stakedCoins_->resetCoins();
    stakedCoins_->resetTimestamp();
}