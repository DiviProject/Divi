#include <BIP9Deployment.h>

BIP9Deployment::BIP9Deployment(
    ): deploymentName("invalid")
    , bit(0)
    , nStartTime(1)
    , nTimeout(0)
    , nPeriod(0)
    , threshold(1)
{
}

BIP9Deployment::BIP9Deployment(
    std::string name,
    unsigned bitIndex, 
    int64_t startTime, 
    int64_t timeout,
    int blockPeriod,
    int blockThreshold
    ): deploymentName(name)
    , bit(static_cast<int>(bitIndex))
    , nStartTime(startTime)
    , nTimeout(timeout)
    , nPeriod(blockPeriod)
    , threshold(blockThreshold)
{
}

BIP9Deployment& BIP9Deployment::operator=(const BIP9Deployment& other)
{
    *const_cast<std::string*>(&deploymentName)=other.deploymentName;
    *const_cast<int*>(&bit)=other.bit;
    *const_cast<int64_t*>(&nStartTime)=other.nStartTime;
    *const_cast<int64_t*>(&nTimeout)=other.nTimeout;
    *const_cast<int*>(&nPeriod)=other.nPeriod;
    *const_cast<int*>(&threshold)=other.threshold;
    return *this;
}

void BIP9Deployment::setState(ThresholdState updatedState) const
{
    state = updatedState;
}