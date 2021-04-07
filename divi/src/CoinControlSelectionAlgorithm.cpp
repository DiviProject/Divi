#include <CoinControlSelectionAlgorithm.h>
#include <coincontrol.h>
#include <WalletTx.h>

CoinControlSelectionAlgorithm::CoinControlSelectionAlgorithm(
    const CCoinControl* coinControl
    ): coinControl_(coinControl)
{
}

std::set<COutput> CoinControlSelectionAlgorithm::SelectCoins(
    const CMutableTransaction& transactionToSelectCoinsFor,
    const std::vector<COutput>& vCoins,
    CAmount& fees) const
{
    CAmount totalInputs = 0;
    std::set<COutput> setCoinsRet;
    if(coinControl_ && coinControl_->HasSelected())
    {
        for(const COutput& out: vCoins)
        {
            if (!out.fSpendable ||
                (!coinControl_->fAllowOtherInputs && !coinControl_->IsSelected(out.tx->GetHash(),out.i)))
            {
                continue;
            }

            totalInputs += out.Value();
            setCoinsRet.insert(out);
        }
    }
    fees = totalInputs - transactionToSelectCoinsFor.GetValueOut();
    return setCoinsRet;
}