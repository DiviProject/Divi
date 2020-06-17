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
    CMasternodeSync& masternodeSynchronization,
    HashedBlockMap& mapHashedBlocks
    ): pwallet_(pwallet)
    , chain_(chain)
    , chainParameters_(chainParameters)
    , peerNotifier_(new PeerNotificationOfMintService(peers))
    , masternodeSync_(masternodeSynchronization)
    , mapHashedBlocks_(mapHashedBlocks)
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

bool CoinMinter::satisfiesMintingRequirements() const
{
    bool stakingRequirements =
        !(chain_.Tip()->nTime < 1471482000 || 
        vNodes.empty() || 
        pwallet_->IsLocked() || 
        nReserveBalance >= pwallet_->GetBalance() || 
        !masternodeSync_.IsSynced());
    if(!stakingRequirements) nLastCoinStakeSearchInterval = 0;
    return stakingRequirements;
}
bool CoinMinter::limitStakingSpeed() const
{
    if (mapHashedBlocks_.count(chain_.Tip()->nHeight)) //search our map of hashed blocks, see if bestblock has been hashed yet
    {
        if (GetTime() - mapHashedBlocks_[chain_.Tip()->nHeight] < std::max(pwallet_->nHashInterval, (unsigned int)1)) // wait half of the nHashDrift with max wait of 3 minutes
        {
            return true;
        }
    }
    return false;
}

bool CoinMinter::isMintable()
{
    if(!hasMintableCoinForProofOfStake()) return false;
    
    return true;
}