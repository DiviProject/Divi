#include <CoinstakeCreator.h>

#include <wallet.h>
#include <kernel.h>
#include <masternode-payments.h>
#include <script/sign.h>
#include <utilmoneystr.h>
#include <SuperblockHelpers.h>

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

    // Mark coin stake transaction
    CScript scriptEmpty;
    scriptEmpty.clear();
    txNew.vout.push_back(CTxOut(0, scriptEmpty));
}

bool CoinstakeCreator::SetSuportedStakingScript(
    const std::pair<const CWalletTx*, unsigned int>& transactionAndIndexPair,
    CAmount stakingReward, 
    CMutableTransaction& txNew)
{
    CScript scriptPubKeyOut = transactionAndIndexPair.first->vout[transactionAndIndexPair.second].scriptPubKey;
    vector<valtype> vSolutions;
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

    //presstab HyperStake - calculate the total size of our new output including the stake reward so that we can use it to decide whether to split the stake outputs
    uint64_t nTotalSize = transactionAndIndexPair.first->vout[transactionAndIndexPair.second].nValue + stakingReward;

    //presstab HyperStake - if MultiSend is set to send in coinstake we will add our outputs here (values asigned further down)
    if (nTotalSize > wallet_.nStakeSplitThreshold * COIN)
        txNew.vout.push_back(CTxOut(0, scriptPubKeyOut)); //split stake

    if (fDebug && GetBoolArg("-printcoinstake", false))
        LogPrintf("CreateCoinStake : added kernel type=%d\n", whichType);

    return true;
}
// ppcoin: create coin stake transaction
bool CoinstakeCreator::CreateCoinStake(
    const CKeyStore& keystore, 
    unsigned int nBits,
    int64_t nSearchInterval,
    CMutableTransaction& txNew,
    unsigned int& nTxNewTime)
{
    // The following split & combine thresholds are important to security
    // Should not be adjusted if you don't understand the consequences
    //int64_t nCombineThreshold = 0;
    if (mapArgs.count("-reservebalance") && !ParseMoney(mapArgs["-reservebalance"], nReserveBalance))
        return error("CreateCoinStake : invalid reserve balance amount");

    CAmount allowedStakingAmount = wallet_.GetBalance() - nReserveBalance;
    MarkTransactionAsCoinstake(txNew);
    // Choose coins to use
    // presstab HyperStake - Initialize as static and don't update the set on every run of CreateCoinStake() in order to lighten resource use
    static std::set<std::pair<const CWalletTx*, unsigned int> > setStakeCoins;
    static int nLastStakeSetUpdate = 0;

    if(!SelectCoins(allowedStakingAmount,nLastStakeSetUpdate,setStakeCoins))
    {
        return false;
    }

    //prevent staking a time that won't be accepted
    if (GetAdjustedTime() <= chainActive.Tip()->nTime)
        MilliSleep(10000);

    vector<const CWalletTx*> vwtxPrev;

    CAmount nCredit = 0;
    CScript scriptPubKeyKernel;

    const CBlockIndex* pIndex0 = chainActive.Tip();
    auto blockSubsidity = GetBlockSubsidity(pIndex0->nHeight + 1);

    BOOST_FOREACH (PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setStakeCoins) 
    {
        //make sure that enough time has elapsed between
        CBlockIndex* pindex = NULL;
        BlockMap::iterator it = mapBlockIndex.find(pcoin.first->hashBlock);
        if (it != mapBlockIndex.end())
        {
            pindex = it->second;
        }
        else 
        {
            if (fDebug) LogPrintf("CreateCoinStake() failed to find block index \n");
            continue;
        }

        // Read block header
        CBlockHeader block = pindex->GetBlockHeader();

        bool fKernelFound = false;
        uint256 hashProofOfStake = 0;
        COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
        nTxNewTime = GetAdjustedTime();


        //iterates each utxo inside of CheckStakeKernelHash()
        if (CheckStakeKernelHash(nBits, block, *pcoin.first, prevoutStake, nTxNewTime, wallet_.nHashDrift, false, hashProofOfStake, true)) 
        {
            //Double check that this will pass time requirements
            if (nTxNewTime <= chainActive.Tip()->GetMedianTimePast()) 
            {
                LogPrintf("CreateCoinStake() : kernel found, but it is too far in the past \n");
                continue;
            }

            // Found a kernel
            if (fDebug && GetBoolArg("-printcoinstake", false))
                LogPrintf("CreateCoinStake : kernel found\n");

            if(!SetSuportedStakingScript(pcoin,blockSubsidity.nStakeReward,txNew))
            {
                break;
            }

            vwtxPrev.push_back(pcoin.first);
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            fKernelFound = true;
            break;
        }
        if (fKernelFound)
            break; // if kernel is found stop searching
    }
    if (nCredit == 0 || nCredit > allowedStakingAmount)
        return false;

    // Calculate reward
    CAmount nReward = blockSubsidity.nStakeReward;
    nCredit += nReward;



    if(txNew.vout.size() == 2)
    {
        const CAmount nCombineThreshold = (wallet_.nStakeSplitThreshold / 2) * COIN;
        using Entry = std::pair<const CWalletTx*, unsigned int>;
        std::vector<Entry> vCombineCandidates;
        for(auto &&pcoin : setStakeCoins)
        {
            // Attempt to add more inputs
            // Only add coins of the same key/address as kernel
            if (pcoin.first->vout[pcoin.second].scriptPubKey == txNew.vout[1].scriptPubKey &&
                    pcoin.first->GetHash() != txNew.vin[0].prevout.hash)
            {
                // Do not add additional significant input
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
            // Stop adding more inputs if already too many inputs
            if (txNew.vin.size() >= MAX_KERNEL_COMBINED_INPUTS)
                break;

            // Stop adding more inputs if value is already pretty significant
            if (nCredit > nCombineThreshold)
                break;

            // Stop adding inputs if reached reserve limit
            if (nCredit + pcoin.first->vout[pcoin.second].nValue > allowedStakingAmount)
                break;

            // Can't add anymore, cause vector is sorted
            if (nCredit + pcoin.first->vout[pcoin.second].nValue > nCombineThreshold)
                break;

            txNew.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nCredit += pcoin.first->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
        }
    }

    // Set output amount
    if (txNew.vout.size() == 3) {
        txNew.vout[1].nValue = nCredit / 2;
        txNew.vout[2].nValue = nCredit - txNew.vout[1].nValue;
    } else {
        txNew.vout[1].nValue = nCredit;
    }

    // Masternode payment
    FillBlockPayee(txNew, blockSubsidity, true);

    // Sign
    int nIn = 0;
    for (const CWalletTx* pcoin : vwtxPrev) {
        if (!SignSignature(wallet_, *pcoin, txNew, nIn++))
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Successfully generated coinstake
    nLastStakeSetUpdate = 0; //this will trigger stake set to repopulate next round
    return true;
}


bool CoinstakeCreator::CreateAndFindStake(
    uint32_t blockBits,
    int64_t nSearchTime, 
    int64_t& nLastCoinStakeSearchTime, 
    CMutableTransaction& txCoinStake,
    unsigned int& nTxNewTime)
{

    bool fStakeFound = false;
    if (nSearchTime >= nLastCoinStakeSearchTime) {
        if (wallet_.CreateCoinStake(wallet_, blockBits, nSearchTime - nLastCoinStakeSearchTime, txCoinStake, nTxNewTime)) {
            fStakeFound = true;
        }
        coinstakeSearchInterval_ = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }
    return fStakeFound;
}