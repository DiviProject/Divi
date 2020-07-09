#include <CoinstakeCreator.h>

#include <wallet.h>
#include <kernel.h>
#include <masternode-payments.h>
#include <script/sign.h>
#include <utilmoneystr.h>
#include <SuperblockHelpers.h>
#include <Settings.h>

extern Settings& settings;

CoinstakeCreator::CoinstakeCreator(
    CWallet& wallet,
    int64_t& coinstakeSearchInterval
    ): wallet_(wallet)
    , coinstakeSearchInterval_(coinstakeSearchInterval)
{
}

bool CoinstakeCreator::SelectCoins(
    CAmount allowedStakingBalance,
    int& nLastStakeSetUpdate, 
    std::set<std::pair<const CWalletTx*, unsigned int> >& setStakeCoins)
{
    if (allowedStakingBalance <= 0)
        return false;

    if (GetTime() - nLastStakeSetUpdate > wallet_.nStakeSetUpdateTime) {
        setStakeCoins.clear();
        if (!wallet_.SelectStakeCoins(setStakeCoins, allowedStakingBalance))
            return false;

        nLastStakeSetUpdate = GetTime();
    }

    if (setStakeCoins.empty())
        return false;

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

bool CoinstakeCreator::SetSuportedStakingScript(
    const std::pair<const CWalletTx*, unsigned int>& transactionAndIndexPair,
    CMutableTransaction& txNew)
{
    CScript scriptPubKeyOut = transactionAndIndexPair.first->vout[transactionAndIndexPair.second].scriptPubKey;
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKeyOut, whichType, vSolutions)) {
        LogPrintf("CreateCoinStake : failed to parse kernel\n");
        return false;
    }
    if (fDebug && GetBoolArg("-printcoinstake", false)) LogPrintf("CreateCoinStake : parsed kernel type=%d\n", whichType);
    if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH) 
    {
        if (fDebug && GetBoolArg("-printcoinstake", false))
            LogPrintf("CreateCoinStake : no support for kernel type=%d\n", whichType);
        return false; // only support pay to public key and pay to address
    }

    txNew.vin.push_back(CTxIn(transactionAndIndexPair.first->GetHash(), transactionAndIndexPair.second));
    txNew.vout.push_back(CTxOut(0, scriptPubKeyOut));

    if (fDebug && GetBoolArg("-printcoinstake", false))
        LogPrintf("CreateCoinStake : added kernel type=%d\n", whichType);

    return true;
}

void CoinstakeCreator::CombineUtxos(
    const CAmount& allowedStakingAmount,
    CMutableTransaction& txNew,
    CAmount& nCredit,
    std::set<std::pair<const CWalletTx*, unsigned int> >& setStakeCoins,
    std::vector<const CWalletTx*>& walletTransactions)
{
    const CAmount nCombineThreshold = (wallet_.nStakeSplitThreshold / 2) * COIN;
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

bool CoinstakeCreator::FindStake(
    unsigned int nBits,
    unsigned int& nTxNewTime,
    std::pair<const CWalletTx*, unsigned int>& stakeData,
    CMutableTransaction& txNew)
{
    BlockMap::iterator it = mapBlockIndex.find(stakeData.first->hashBlock);
    if (it == mapBlockIndex.end())
    {
        if (fDebug) LogPrintf("CreateCoinStake() failed to find block index \n");
        return false;
    }

    uint256 hashProofOfStake = 0;
    nTxNewTime = GetAdjustedTime();

    if (CheckStakeKernelHash(
            nBits,
            it->second->GetBlockHeader(),
            *stakeData.first,
            COutPoint(stakeData.first->GetHash(), stakeData.second),
            nTxNewTime,
            wallet_.nHashDrift,
            false,
            hashProofOfStake,
            true))
    {
        if (nTxNewTime <= chainActive.Tip()->GetMedianTimePast())
        {
            LogPrintf("CreateCoinStake() : kernel found, but it is too far in the past \n");
            return false;
        }
        if (fDebug && GetBoolArg("-printcoinstake", false))
            LogPrintf("CreateCoinStake : kernel found\n");

        if(!SetSuportedStakingScript(stakeData,txNew))
        {
            return false;
        }

        return true;
    }
    return false;
}

bool CoinstakeCreator::CreateCoinStake(
    const CKeyStore& keystore, 
    unsigned int nBits,
    int64_t nSearchInterval,
    CMutableTransaction& txNew,
    unsigned int& nTxNewTime)
{
    if (settings.ParameterIsSet("-reservebalance") && !ParseMoney(settings.GetParameter("-reservebalance"), nReserveBalance))
        return error("CreateCoinStake : invalid reserve balance amount");

    CAmount allowedStakingAmount = wallet_.GetBalance() - nReserveBalance;
    MarkTransactionAsCoinstake(txNew);

    static std::set<std::pair<const CWalletTx*, unsigned int> > setStakeCoins;
    static int nLastStakeSetUpdate = 0;
    if(!SelectCoins(allowedStakingAmount,nLastStakeSetUpdate,setStakeCoins)) return false;
    if (GetAdjustedTime() <= chainActive.Tip()->nTime) MilliSleep(10000);

    std::vector<const CWalletTx*> vwtxPrev;
    CAmount nCredit = 0;
    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto blockSubsidity = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(chainActive.Tip()->nHeight + 1);

    BOOST_FOREACH (PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setStakeCoins) 
    {
        if(FindStake(nBits, nTxNewTime, pcoin,txNew))
        {
            vwtxPrev.push_back(pcoin.first);
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            break;
        }
    }
    if (nCredit == 0 || nCredit > allowedStakingAmount)
        return false;

    CAmount nReward = blockSubsidity.nStakeReward;
    nCredit += nReward;
    if (nCredit > static_cast<CAmount>(wallet_.nStakeSplitThreshold) * COIN) 
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

    FillBlockPayee(txNew, blockSubsidity, true);

    int nIn = 0;
    for (const CWalletTx* pcoin : vwtxPrev) {
        if (!SignSignature(wallet_, *pcoin, txNew, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    nLastStakeSetUpdate = 0; //this will trigger stake set to repopulate next round
    return true;
}

bool CoinstakeCreator::CreateProofOfStake(
    uint32_t blockBits,
    int64_t nSearchTime, 
    int64_t& nLastCoinStakeSearchTime, 
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        if (CreateCoinStake(wallet_, blockBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime)) 
        {
            fStakeFound = true;
        }
        coinstakeSearchInterval_ = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}