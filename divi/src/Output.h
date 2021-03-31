#ifndef OUTPUT_H
#define OUTPUT_H
#include <amount.h>
#include <string>
class CWalletTx;
class CScript;
class COutput
{
public:
    const CWalletTx* tx;
    int i;
    int nDepth;
    bool fSpendable;

    COutput();
    COutput(const CWalletTx* txIn, int iIn, int nDepthIn, bool fSpendableIn);

    bool IsValid() const;
    CAmount Value() const;
    const CScript& scriptPubKey() const;
    std::string ToString() const;

    bool operator<(const COutput& other) const
    {
        return tx < other.tx ||
            (tx == other.tx && i < other.i);
    }
    bool operator==(const COutput& other) const
    {
        return tx == other.tx && i == other.i;
    }
};

#endif// OUTPUT_H