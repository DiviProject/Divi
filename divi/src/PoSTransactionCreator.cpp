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

extern Settings& settings;
extern const int nHashDrift;
extern const unsigned int MAX_KERNEL_COMBINED_INPUTS;
extern const int maximumFutureBlockDrift = 180; // seconds
extern BlockMap mapBlockIndex;
extern CChain chainActive;

PoSTransactionCreator::PoSTransactionCreator(
    CWallet& wallet,
    int64_t& coinstakeSearchInterval,
    std::map<unsigned int, unsigned int>& hashedBlockTimestamps
    ): wallet_(wallet)
    , coinstakeSearchInterval_(coinstakeSearchInterval)
    , hashedBlockTimestamps_(hashedBlockTimestamps)
{
}

bool PoSTransactionCreator::SelectCoins(
    const CChainParams& chainParameters,
    CAmount allowedStakingBalance,
    int& nLastStakeSetUpdate,
    std::set<std::pair<const CWalletTx*, unsigned int> >& setStakeCoins)
{
    if (allowedStakingBalance <= 0)
        return false;

    if (chainParameters.NetworkID() == CBaseChainParams::REGTEST ||
        GetTime() - nLastStakeSetUpdate > settings.GetArg("-stakeupdatetime",300))
    {
        setStakeCoins.clear();
        if (!wallet_.SelectStakeCoins(setStakeCoins, allowedStakingBalance)) {
            return error("failed to select coins for staking");
        }

        nLastStakeSetUpdate = GetTime();
    }

    if (setStakeCoins.empty()) {
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
    std::set<std::pair<const CWalletTx*, unsigned int> >& setStakeCoins,
    std::vector<const CWalletTx*>& walletTransactions)
{
    const CAmount nCombineThreshold = (settings.GetArg("-stakesplitthreshold",100000) / 2) * COIN;
    using Entry = std::pair<const CWalletTx*, unsigned int>;
    std::vector<Entry> vCombineCandidates;
    for(auto &&pcoin : setStakeCoins)
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
    std::pair<const CWalletTx*, unsigned int>& stakeData,
    CMutableTransaction& txNew)
{
    BlockMap::const_iterator it = mapBlockIndex.find(stakeData.first->hashBlock);
    if (it == mapBlockIndex.end())
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
    HashproofCreationResult hashproofResult = CreateHashproofTimestamp(stakingData,nTxNewTime);
    if(!hashproofResult.failedAtSetup())
    {
        hashedBlockTimestamps_.clear();
        hashedBlockTimestamps_[chainActive.Height()] = GetTime();
    }
    if (hashproofResult.succeeded())
    {
        if (hashproofResult.timestamp() <= chainActive.Tip()->GetMedianTimePast())
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

bool PoSTransactionCreator::PopulateCoinstakeTransaction(
    const CKeyStore& keystore,
    unsigned int nBits,
    int64_t nSearchInterval,
    CMutableTransaction& txNew,
    unsigned int& nTxNewTime)
{
    CAmount allowedStakingAmount = wallet_.GetStakingBalance();
    MarkTransactionAsCoinstake(txNew);

    static std::set<std::pair<const CWalletTx*, unsigned int> > setStakeCoins;
    static int nLastStakeSetUpdate = 0;
    const CChainParams& chainParameters = Params();
    if(!SelectCoins(chainParameters, allowedStakingAmount,nLastStakeSetUpdate,setStakeCoins)) return false;

    auto adjustedTime = GetAdjustedTime();
    int64_t minimumTime = chainActive.Tip()->GetMedianTimePast() + 1;
    const int64_t maximumTime = minimumTime + maximumFutureBlockDrift - 1;
    int64_t drift = chainParameters.RetargetDifficulty()? nHashDrift: 0;
    nTxNewTime = std::min(std::max(adjustedTime, minimumTime+drift), maximumTime);

    std::vector<const CWalletTx*> vwtxPrev;
    CAmount nCredit = 0;
    SuperblockSubsidyContainer subsidyContainer(chainParameters);

    const CBlockIndex* chainTip = chainActive.Tip();
    int chainTipHeight = chainTip->nHeight;
    int newBlockHeight = chainTipHeight + 1;
    auto blockSubsidity = subsidyContainer.blockSubsidiesProvider().GetBlockSubsidity(newBlockHeight);

    BOOST_FOREACH (PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setStakeCoins)
    {
        if(!IsSupportedScript(pcoin.first->vout[pcoin.second].scriptPubKey))
        {
            continue;
        }

        if(FindHashproof(nBits, nTxNewTime, pcoin,txNew) )
        {
            if(chainActive.Height() == chainTipHeight)
            {
                vwtxPrev.push_back(pcoin.first);
                nCredit += pcoin.first->vout[pcoin.second].nValue;
            }
            break;
        }

    }
    if (nCredit == 0 || nCredit > allowedStakingAmount)
        return false;

    CAmount nReward = blockSubsidity.nStakeReward;
    nCredit += nReward;
    if (nCredit > static_cast<CAmount>(settings.GetArg("-stakesplitthreshold",100000)) * COIN)
    {
        txNew.vout.push_back(txNew.vout.back());
        txNew.vout[1].nValue = nCredit / 2;
        txNew.vout[2].nValue = nCredit - txNew.vout[1].nValue;
    }
    else
    {
        CombineUtxos(allowedStakingAmount,txNew,nCredit,setStakeCoins,vwtxPrev);
        txNew.vout[1].nValue = nCredit;
    }

    BlockIncentivesPopulator(
        chainParameters,
        chainActive,
        masternodePayments,
        subsidyContainer.superblockHeightValidator(),
        subsidyContainer.blockSubsidiesProvider())
        .FillBlockPayee(txNew,blockSubsidity,newBlockHeight,true);

    int nIn = 0;
    for (const CWalletTx* pcoin : vwtxPrev) {
        if (!SignSignature(wallet_, *pcoin, txNew, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    nLastStakeSetUpdate = 0; //this will trigger stake set to repopulate next round
    return true;
}

bool PoSTransactionCreator::CreateProofOfStake(
    uint32_t blockBits,
    int64_t nSearchTime,
    int64_t& nLastCoinStakeSearchTime,
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        if (PopulateCoinstakeTransaction(wallet_, blockBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime))
        {
            fStakeFound = true;
        }
        coinstakeSearchInterval_ = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}
