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

class StakedCoins
{
private:
    std::set<StakableCoin> underlyingSet_;
    int64_t timestampOfLastUpdate_;
public:
    StakedCoins(): underlyingSet_(), timestampOfLastUpdate_(0)
    {
    }
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
    I_StakingWallet& wallet,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps
    ): settings_(settings)
    , chainParameters_(chainParameters)
    , activeChain_(activeChain)
    , mapBlockIndex_(mapBlockIndex)
    , blockSubsidies_( blockSubsidies )
    , incentives_(incentives)
    , proofGenerator_(proofGenerator )
    , stakedCoins_(new StakedCoins())
    , wallet_(wallet)
    , hashedBlockTimestamps_(hashedBlockTimestamps)
    , hashproofTimestampMinimumValue_(0)
{
}

PoSTransactionCreator::~PoSTransactionCreator()
{
    stakedCoins_.reset();
}

bool PoSTransactionCreator::SelectCoins()
{
    if (chainParameters_.NetworkID() == CBaseChainParams::REGTEST ||
        GetTime() - stakedCoins_->timestamp() > settings_.GetArg("-stakeupdatetime",300))
    {
        stakedCoins_->asSet().clear();
        if (!wallet_.SelectStakeCoins(stakedCoins_->asSet())) {
            return error("failed to select coins for staking");
        }

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
    LogPrint("staking","%s : parsed kernel type=%d\n",__func__, whichType);
    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH && whichType != TX_VAULT)
    {
        LogPrint("staking","%s : no support for kernel type=%d\n",__func__, whichType);
        return false; // only support pay to public key and pay to address
    }
    isVaultScript =whichType == TX_VAULT;
    return true;
}

bool PoSTransactionCreator::SetSuportedStakingScript(
    const StakableCoin& stakableCoin,
    CMutableTransaction& txNew)
{
    txNew.vin.emplace_back(stakableCoin.utxo);
    txNew.vout.emplace_back(0, stakableCoin.GetTxOut().scriptPubKey);

    return true;
}

void PoSTransactionCreator::CombineUtxos(
    const CAmount& stakeSplit,
    CMutableTransaction& txNew,
    CAmount& nCredit,
    std::vector<const CTransaction*>& walletTransactions)
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
    CMutableTransaction& txNew)
{
    BlockMap::const_iterator it = mapBlockIndex_.find(stakeData.blockHashOfFirstConfirmation);
    if (it == mapBlockIndex_.end())
    {
        LogPrint("staking","%s failed to find block index for %s\n",__func__,stakeData.blockHashOfFirstConfirmation);
        return false;
    }

    StakingData stakingData(
        nBits,
        static_cast<unsigned>(it->second->GetBlockTime()),
        it->second->GetBlockHash(),
        stakeData.utxo,
        stakeData.GetTxOut().nValue,
        chainTip->GetBlockHash());
    HashproofCreationResult hashproofResult = proofGenerator_.CreateHashproofTimestamp(stakingData,nTxNewTime);
    if(!hashproofResult.failedAtSetup())
    {
        hashedBlockTimestamps_.clear();
        hashedBlockTimestamps_[chainTip->nHeight] = GetTime();
    }
    if (hashproofResult.succeeded())
    {
        if (hashproofResult.timestamp() <= chainTip->GetMedianTimePast())
        {
            LogPrintf("%s : kernel found, but it is too far in the past \n",__func__);
            return false;
        }
        LogPrint("staking","%s : kernel found for %s\n",__func__, stakeData.tx->ToStringShort());

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
    bool& isVaultScript)
{
    for (const StakableCoin& pcoin: stakedCoins_->asSet())
    {
        if(!IsSupportedScript(pcoin.GetTxOut().scriptPubKey,isVaultScript))
        {
            continue;
        }
        if(chainTip->nHeight != activeChain_.Height())
        {
            hashproofTimestampMinimumValue_ = 0;
            return nullptr;
        }
        if(FindHashproof(chainTip,blockBits, nTxNewTime, pcoin,txCoinStake) )
        {
            return &pcoin;
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
    std::vector<const CTransaction*>& vwtxPrev)
{
    CBlockRewards blockSubdidy = blockSubsidies_.GetBlockSubsidity(chainTip->nHeight + 1);
    CAmount nCredit = stakeData.GetTxOut().nValue + blockSubdidy.nStakeReward;
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
    CMutableTransaction& txCoinStake)
{
    CBlockRewards blockSubdidy = blockSubsidies_.GetBlockSubsidity(chainTip->nHeight + 1);
    incentives_.FillBlockPayee(txCoinStake,blockSubdidy,chainTip);
}

bool PoSTransactionCreator::CreateProofOfStake(
    const CBlockIndex* chainTip,
    uint32_t blockBits,
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{
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
        if (!SignSignature(wallet_, *pcoin, txCoinStake, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    stakedCoins_->resetTimestamp(); //this will trigger stake set to repopulate next round
    return true;
}
