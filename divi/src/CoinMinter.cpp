#include <CoinMinter.h>
#include <wallet.h>
#include <utiltime.h>
#include <chain.h>
#include <chainparams.h>
#include <PeerNotificationOfMintService.h>
#include <masternode-sync.h>

CoinMinter::CoinMinter(
    CWallet* pwallet,
    CChain& chain,
    const CChainParams& chainParameters,
    std::vector<CNode*>& peers,
    CMasternodeSync& masternodeSynchronization
    ): pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , peerNotifier_(new PeerNotificationOfMintService(peers))
    , masternodeSync_(masternodeSynchronization)
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
bool CoinMinter::isAtProofOfStakeHeight() const
{
    return chain_.Tip()->nHeight >= chainParameters_.LAST_POW_BLOCK();
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