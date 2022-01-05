#ifndef WALLET_RESCANNER_H
#define WALLET_RESCANNER_H
class I_BlockDataReader;
class CBlockIndex;
class CChain;
class CWallet;
class CCriticalSection;

class WalletRescanner
{
    const I_BlockDataReader& blockReader_;
    const CChain& activeChain_;
    CCriticalSection& mainCS_;
public:
    WalletRescanner(const I_BlockDataReader& blockReader, const CChain& activeChain, CCriticalSection& mainCS);
    void scanForWalletTransactions(CWallet& wallet, const CBlockIndex* pindexStart);
};
#endif// WALLET_RESCANNER_H
