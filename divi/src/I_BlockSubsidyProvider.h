#ifndef I_BLOCK_SUBSIDY_PROVIDER_H
#define I_BLOCK_SUBSIDY_PROVIDER_H
#include <BlockRewards.h>
#include <amount.h>

class I_BlockSubsidyProvider
{
public:
    virtual ~I_BlockSubsidyProvider(){}
    virtual CBlockRewards GetBlockSubsidity(int nHeight) const = 0;
    virtual CAmount GetFullBlockValue(int nHeight) const = 0;
};
#endif // I_BLOCK_SUBSIDY_PROVIDER_H