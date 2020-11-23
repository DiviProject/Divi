#include <PoSTransactionCreator.h>

#include <wallet.h>
#include <ProofOfStakeGenerator.h>
#include <masternode-payments.h>
#include <script/sign.h>
#include <utilmoneystr.h>
#include <SuperblockHelpers.h>
#include <Settings.h>
#include <BlockIncentivesPopulator.h>
#include <blockmap.h>
#include <chain.h>
#include <chainparams.h>
#include <StakingData.h>
#include <I_PoSStakeModifierService.h>
#include <Logging.h>
#include <utiltime.h>

extern Settings& settings;
extern const int nHashDrift;
extern const unsigned int MAX_KERNEL_COMBINED_INPUTS;
extern const int maximumFutureBlockDrift = 180; // seconds
extern bool fDebug;

class StakedCoins
{
private:
    std::set<std::pair<const CWalletTx*, unsigned int>> underlyingSet_;
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
    std::set<std::pair<const CWalletTx*, unsigned int>>& asSet()
    {
        return underlyingSet_;
    }
};

PoSTransactionCreator::PoSTransactionCreator(
    const CChainParams& chainParameters,
    CChain& activeChain,
    const BlockMap& mapBlockIndex,
    const I_PoSStakeModifierService& stakeModifierService,
    const I_BlockSubsidyProvider& blockSubsidies,
    const BlockIncentivesPopulator& incentives,
    CWallet& wallet,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps
    ): chainParameters_(chainParameters)
    , activeChain_(activeChain)
    , mapBlockIndex_(mapBlockIndex)
    , blockSubsidies_( blockSubsidies )
    , incentives_(incentives)
    , proofGenerator_(new ProofOfStakeGenerator(stakeModifierService, chainParameters_.GetMinCoinAgeForStaking()) )
    , stakedCoins_(new StakedCoins())
    , wallet_(wallet)
    , hashedBlockTimestamps_(hashedBlockTimestamps)
    , hashproofTimestampMinimumValue_(0)
{
}

PoSTransactionCreator::~PoSTransactionCreator()
{
    stakedCoins_.reset();
    proofGenerator_.reset();
}

bool PoSTransactionCreator::SelectCoins(CAmount allowedStakingBalance)
{
    if (allowedStakingBalance <= 0)
        return false;

    if (chainParameters_.NetworkID() == CBaseChainParams::REGTEST ||
        GetTime() - stakedCoins_->timestamp() > settings.GetArg("-stakeupdatetime",300))
    {
        stakedCoins_->asSet().clear();
        if (!wallet_.SelectStakeCoins(stakedCoins_->asSet(), allowedStakingBalance)) {
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
    const CScript& scriptPubKeyOut)
{
    txnouttype whichType;
    std::vector<valtype> vSolutions;
    if (!ExtractScriptPubKeyFormat(scriptPubKeyOut, whichType, vSolutions)) {
        LogPrintf("CreateCoinStake : failed to parse kernel\n");
        return false;
    }
    if (fDebug && settings.GetBoolArg("-printcoinstake", false)) LogPrintf("CreateCoinStake : parsed kernel type=%d\n", whichType);
    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
    {
        if (fDebug && settings.GetBoolArg("-printcoinstake", false))
            LogPrintf("CreateCoinStake : no support for kernel type=%d\n", whichType);
        return false; // only support pay to public key and pay to address
    }
    return true;
}

bool PoSTransactionCreator::SetSuportedStakingScript(
    const std::pair<const CWalletTx*, unsigned int>& transactionAndIndexPair,
    CMutableTransaction& txNew)
{
    txNew.vin.push_back(CTxIn(transactionAndIndexPair.first->GetHash(), transactionAndIndexPair.second));
    txNew.vout.push_back(CTxOut(0, transactionAndIndexPair.first->vout[transactionAndIndexPair.second].scriptPubKey ));

    return true;
}

void PoSTransactionCreator::CombineUtxos(
    const CAmount& allowedStakingAmount,
    CMutableTransaction& txNew,
    CAmount& nCredit,
    std::vector<const CWalletTx*>& walletTransactions)
{
    const CAmount nCombineThreshold = (settings.GetArg("-stakesplitthreshold",100000) / 2) * COIN;
    using Entry = std::pair<const CWalletTx*, unsigned int>;
    std::vector<Entry> vCombineCandidates;
    for(const Entry& pcoin : stakedCoins_->asSet())
    {
        if (pcoin.first->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey &&
                pcoin.first->GetHash() != txNew.vin[0].prevout.hash)
        {
            if (pcoin.first->vout[pcoin.second].nValue + nCredit > nCombineThreshold)
                continue;

            vCombineCandidates.push_back(pcoin);
        }
    }

    std::sort(std::begin(vCombineCandidates), std::end(vCombineCandidates), [](const Entry &left, const Entry &right) {
        return left.first->vout[left.second].nValue < right.first->vout[right.second].nValue;
    });

    for(auto &&pcoin : vCombineCandidates)
    {
        if (txNew.vin.size() >= MAX_KERNEL_COMBINED_INPUTS||
            nCredit > nCombineThreshold ||
            nCredit + pcoin.first->vout[pcoin.second].nValue > allowedStakingAmount ||
            nCredit + pcoin.first->vout[pcoin.second].nValue > nCombineThreshold)
            break;

        txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
        nCredit += pcoin.first->vout[pcoin.second].nValue;
        walletTransactions.push_back(pcoin.first);
    }
}

bool PoSTransactionCreator::FindHashproof(
    unsigned int nBits,
    unsigned int& nTxNewTime,
    const std::pair<const CWalletTx*, unsigned int>& stakeData,
    CMutableTransaction& txNew)
{
    BlockMap::const_iterator it = mapBlockIndex_.find(stakeData.first->hashBlock);
    if (it == mapBlockIndex_.end())
    {
        if (fDebug) LogPrintf("CreateCoinStake() failed to find block index \n");
        return false;
    }

    StakingData stakingData(
        nBits,
        static_cast<unsigned>(it->second->GetBlockTime()),
        it->second->GetBlockHash(),
        COutPoint(stakeData.first->GetHash(), stakeData.second),
        stakeData.first->vout[stakeData.second].nValue);
    HashproofCreationResult hashproofResult = proofGenerator_->CreateHashproofTimestamp(stakingData,nTxNewTime);
    if(!hashproofResult.failedAtSetup())
    {
        hashedBlockTimestamps_.clear();
        hashedBlockTimestamps_[activeChain_.Height()] = GetTime();
    }
    if (hashproofResult.succeeded())
    {
        if (hashproofResult.timestamp() <= activeChain_.Tip()->GetMedianTimePast())
        {
            LogPrintf("CreateCoinStake() : kernel found, but it is too far in the past \n");
            return false;
        }
        if (fDebug && settings.GetBoolArg("-printcoinstake", false))
            LogPrintf("CreateCoinStake : kernel found\n");

        if(!SetSuportedStakingScript(stakeData,txNew))
        {
            return false;
        }
        nTxNewTime = hashproofResult.timestamp();
        return true;
    }
    return false;
}

std::pair<const CWalletTx*, CAmount> PoSTransactionCreator::FindProofOfStake(
    uint32_t blockBits,
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{
    using Entry = std::pair<const CWalletTx*, unsigned int>;
    for (const Entry& pcoin: stakedCoins_->asSet())
    {
        if(!IsSupportedScript(pcoin.first->vout[pcoin.second].scriptPubKey))
        {
            continue;
        }
        if(FindHashproof(blockBits, nTxNewTime, pcoin,txCoinStake) )
        {
            return std::make_pair(pcoin.first,pcoin.first->vout[pcoin.second].nValue);
        }
    }
    return std::make_pair(nullptr,0);
}

bool PoSTransactionCreator::CreateProofOfStake(
    uint32_t blockBits,
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{
    CAmount allowedStakingAmount = wallet_.GetStakingBalance();
    MarkTransactionAsCoinstake(txCoinStake);

    if(!SelectCoins(allowedStakingAmount)) return false;

    int64_t adjustedTime = GetAdjustedTime();
    int64_t minimumTime = activeChain_.Tip()->GetMedianTimePast() + 1;
    const int64_t maximumTime = minimumTime + maximumFutureBlockDrift - 1;
    int64_t drift = chainParameters_.RetargetDifficulty()? nHashDrift: 0;
    nTxNewTime = std::min(std::max(adjustedTime, minimumTime+drift), maximumTime);

    const CBlockIndex* chainTip = activeChain_.Tip();
    int newBlockHeight = chainTip->nHeight + 1;
    auto blockSubsidity = blockSubsidies_.GetBlockSubsidity(newBlockHeight);

    std::pair<const CWalletTx*, CAmount> successfullyStakableUTXO =
        FindProofOfStake(blockBits,txCoinStake,nTxNewTime);

    CAmount nCredit = successfullyStakableUTXO.second;
    std::vector<const CWalletTx*> vwtxPrev(1, successfullyStakableUTXO.first);
    if( successfullyStakableUTXO.first == nullptr)
    {
        return false;
    }


    if (nCredit == 0 || nCredit > allowedStakingAmount)
        return false;

    CAmount nReward = blockSubsidity.nStakeReward;
    nCredit += nReward;
    if (nCredit > static_cast<CAmount>(settings.GetArg("-stakesplitthreshold",100000)) * COIN)
    {
        txCoinStake.vout.push_back(txCoinStake.vout.back());
        txCoinStake.vout[1].nValue = nCredit / 2;
        txCoinStake.vout[2].nValue = nCredit - txCoinStake.vout[1].nValue;
    }
    else
    {
        CombineUtxos(allowedStakingAmount,txCoinStake,nCredit,vwtxPrev);
        txCoinStake.vout[1].nValue = nCredit;
    }

    incentives_.FillBlockPayee(txCoinStake,blockSubsidity,newBlockHeight,true);

    int nIn = 0;
    for (const CWalletTx* pcoin : vwtxPrev) {
        if (!SignSignature(wallet_, *pcoin, txCoinStake, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    stakedCoins_->resetTimestamp(); //this will trigger stake set to repopulate next round
    return true;
}