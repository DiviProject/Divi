#include <Output.h>
#include <WalletTx.h>
#include <utilmoneystr.h>

COutput::COutput(
    ): tx(nullptr)
    , i(-1)
    ,nDepth(-1)
    ,fSpendable(false)
{
}

COutput::COutput(const CWalletTx* txIn, int iIn, int nDepthIn, bool fSpendableIn)
{
    tx = txIn;
    i = iIn;
    nDepth = nDepthIn;
    fSpendable = fSpendableIn;
}

bool COutput::IsValid() const
{
    return !(tx == nullptr || i < 0);
}

CAmount COutput::Value() const
{
    return tx->vout[i].nValue;
}

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}