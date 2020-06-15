#ifndef MOCK_BLOCK_SUBSIDY_PROVIDER_H
#define MOCK_BLOCK_SUBSIDY_PROVIDER_H
#include <I_BlockSubsidyProvider.h>
#include <gmock/gmock.h>
class MockBlockSubsidyProvider: public I_BlockSubsidyProvider
{
public:
    MOCK_CONST_METHOD1(GetBlockSubsidity, CBlockRewards(int nHeight));
    MOCK_CONST_METHOD1(GetFullBlockValue, CAmount(int nHeight));
};
#endif // MOCK_BLOCK_SUBSIDY_PROVIDER_H