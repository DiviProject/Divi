#include <walletdustcombiner.h>
#include <wallet.h>
#include <Logging.h>
#include <coincontrol.h>
#include <chainparams.h>
#include <WalletTx.h>
#include <CoinControlSelectionAlgorithm.h>
#include <SignatureSizeEstimator.h>
#include <Settings.h>
#include <primitives/transaction.h>
#include <version.h>
#include <FeeRate.h>
#include <FeeAndPriorityCalculator.h>

extern CWallet* pwalletMain;

static CAmount FeeEstimates(
    const CFeeRate& feeRate,
    const CKeyStore& keyStore,
    const std::vector<std::pair<CScript, CAmount> >& intendedDestinations,
    const std::vector<COutput>& outputsToCombine)
{
    constexpr unsigned costOfMaybeIncludingChangeAddress = 34u;
    CMutableTransaction txNew;
    txNew.vout.clear();
    for(const std::pair<CScript, CAmount>& s: intendedDestinations)
    {
        txNew.vout.emplace_back(s.second,s.first);
    }
    const unsigned initialTxSize = ::GetSerializeSize(CTransaction(txNew),SER_NETWORK,PROTOCOL_VERSION);
    SignatureSizeEstimator estimator;
    unsigned scriptSigSize = 0u;
    for(const auto& output: outputsToCombine)
    {
        scriptSigSize += estimator.MaxBytesNeededForScriptSig(keyStore,output.scriptPubKey());
    }
    return feeRate.GetFee(initialTxSize + scriptSigSize + costOfMaybeIncludingChangeAddress);
}

WalletDustCombiner::WalletDustCombiner(
    CWallet& wallet,
    const CFeeRate& relayFeeRate
    ): wallet_(wallet)
    , relayFeeRate_(relayFeeRate)
{
}

void WalletDustCombiner::CombineDust(CAmount combineThreshold)
{
    if (wallet_.IsLocked()) {
        return;
    }

    std::map<CBitcoinAddress, std::vector<COutput> > mapCoinsByAddress = wallet_.AvailableCoinsByAddress(true, 0);

    //coins are sectioned by address. This combination code only wants to combine inputs that belong to the same address
    for (std::map<CBitcoinAddress, std::vector<COutput> >::iterator it = mapCoinsByAddress.begin(); it != mapCoinsByAddress.end(); it++)
    {
        std::vector<COutput> coinsToCombine;
        //find masternode rewards that need to be combined
        CCoinControl* coinControl = new CCoinControl();
        CAmount nTotalRewardsValue = 0;
        for(const COutput& out: it->second)
        {
            //no coins should get this far if they dont have proper maturity, this is double checking
            if (out.tx->IsCoinStake() && out.tx->GetNumberOfBlockConfirmations() < Params().COINBASE_MATURITY() + 1)
                continue;

            if (out.Value() > combineThreshold * COIN)
                continue;

            COutPoint outpt(out.tx->GetHash(), out.i);
            coinControl->Select(outpt);
            coinsToCombine.push_back(out);
            nTotalRewardsValue += out.Value();
        }

        //if no inputs found then return
        if (!coinControl->HasSelected())
            continue;

        //we cannot combine one coin with itself
        if (coinsToCombine.size() <= 1)
            continue;

        std::vector<std::pair<CScript, CAmount> > vecSend;
        CScript scriptPubKey = GetScriptForDestination(it->first.Get());
        CAmount expectedFee = FeeEstimates(relayFeeRate_,wallet_,vecSend,coinsToCombine);
        vecSend.push_back(std::make_pair(scriptPubKey, nTotalRewardsValue-expectedFee));

        // Create the transaction and commit it to the network
        CWalletTx wtx;
        std::pair<std::string,bool> txCreationResult;
        {
            CoinControlSelectionAlgorithm coinSelectionAlgorithm(coinControl);
            txCreationResult = wallet_.SendMoney(vecSend, wtx, ALL_SPENDABLE_COINS,&coinSelectionAlgorithm);
        }
        delete coinControl;
        if (!txCreationResult.second) {
            LogPrintf("CombineDust transaction failed, reason: %s\n", txCreationResult.first);
            continue;
        }
        LogPrintf("CombineDust sent transaction\n");
    }
}

void combineWalletDust(const Settings& settings)
{
    if (pwalletMain) {
        // If turned on Auto Combine will scan wallet for dust to combine
        if (settings.ParameterIsSet("-combinethreshold"))
        {
            static WalletDustCombiner dustCombiner(*pwalletMain,FeeAndPriorityCalculator::instance().getFeeRateQuote());
            dustCombiner.CombineDust(
                settings.GetArg("-combinethreshold",std::numeric_limits<int64_t>::max() ) );
        }
    }
}