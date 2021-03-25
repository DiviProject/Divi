#include <walletdustcombiner.h>
#include <wallet.h>
#include <Logging.h>
#include <coincontrol.h>
#include <chainparams.h>
#include <WalletTx.h>
#include <CoinControlSelectionAlgorithm.h>

bool IsInitialBlockDownload();

WalletDustCombiner::WalletDustCombiner(
    CWallet& wallet
    ): wallet_(wallet)
{
}

void WalletDustCombiner::CombineDust(CAmount combineThreshold)
{
    if (IsInitialBlockDownload() || wallet_.IsLocked()) {
        return;
    }

    std::map<CBitcoinAddress, std::vector<COutput> > mapCoinsByAddress = wallet_.AvailableCoinsByAddress(true, 0);

    //coins are sectioned by address. This combination code only wants to combine inputs that belong to the same address
    for (std::map<CBitcoinAddress, std::vector<COutput> >::iterator it = mapCoinsByAddress.begin(); it != mapCoinsByAddress.end(); it++) {
        std::vector<COutput> vCoins, vRewardCoins;
        vCoins = it->second;

        //find masternode rewards that need to be combined
        CCoinControl* coinControl = new CCoinControl();
        CAmount nTotalRewardsValue = 0;
        BOOST_FOREACH (const COutput& out, vCoins) {
            //no coins should get this far if they dont have proper maturity, this is double checking
            if (out.tx->IsCoinStake() && out.tx->GetNumberOfBlockConfirmations() < Params().COINBASE_MATURITY() + 1)
                continue;

            if (out.Value() > combineThreshold * COIN)
                continue;

            COutPoint outpt(out.tx->GetHash(), out.i);
            coinControl->Select(outpt);
            vRewardCoins.push_back(out);
            nTotalRewardsValue += out.Value();
        }

        //if no inputs found then return
        if (!coinControl->HasSelected())
            continue;

        //we cannot combine one coin with itself
        if (vRewardCoins.size() <= 1)
            continue;

        std::vector<std::pair<CScript, CAmount> > vecSend;
        CScript scriptPubKey = GetScriptForDestination(it->first.Get());
        vecSend.push_back(std::make_pair(scriptPubKey, nTotalRewardsValue));

        // Create the transaction and commit it to the network
        CWalletTx wtx;
        CReserveKey keyChange(wallet_); // this change address does not end up being used, because change is returned with coin control switch
        std::string strErr;

        CoinControlSelectionAlgorithm coinSelectionAlgorithm(coinControl);
        if (!wallet_.CreateTransaction(vecSend, wtx, keyChange, strErr, ALL_SPENDABLE_COINS,&coinSelectionAlgorithm)) {
            LogPrintf("CombineDust createtransaction failed, reason: %s\n", strErr);
            continue;
        }

        if (!wallet_.CommitTransaction(wtx, keyChange)) {
            LogPrintf("CombineDust transaction commit failed\n");
            continue;
        }

        LogPrintf("CombineDust sent transaction\n");

        delete coinControl;
    }
}
