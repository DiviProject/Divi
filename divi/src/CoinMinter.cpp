#include <CoinMinter.h>
#include <wallet.h>
#include <utiltime.h>

CoinMinter::CoinMinter(
    CWallet* pwallet
    ): pwallet_(pwallet)
    , haveMintableCoins_(false)
    , lastTimeCheckedMintable_(0)
    , timeToWait_(0)
{
}

bool CoinMinter::hasMintableCoinForProofOfStake()
{
    int timeWaited = GetTime() - lastTimeCheckedMintable_;
    
    if(timeWaited > fiveMinutes_)
    {
        lastTimeCheckedMintable_ = GetTime();
        haveMintableCoins_ = pwallet_->MintableCoins();
    }
    else
    {
        timeToWait_ = fiveMinutes_ - timeWaited;
    }
    return haveMintableCoins_;
}
const int64_t& CoinMinter::getTimeTillNextCheck() const
{
    return timeToWait_;
}
bool CoinMinter::isMintable()
{
    if(!hasMintableCoinForProofOfStake()) return false;
    
    return true;
}