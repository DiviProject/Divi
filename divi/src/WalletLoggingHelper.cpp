#include <WalletLoggingHelper.h>
#include <wallet.h>
#include <Logging.h>
#include <string>

std::string ValueFromCAmount(const CAmount& amount)
{
    bool sign = amount < 0;
    int64_t n_abs = (sign ? -amount : amount);
    int64_t quotient = n_abs / COIN;
    int64_t remainder = n_abs % COIN;
    return strprintf("%s%d.%08d", sign ? "-" : "", quotient, remainder);
}

void LogWalletBalance(CWallet* pwallet)
{
    if(pwallet)
    {
        LogPrintStr("; balance = " + ValueFromCAmount(pwallet->GetBalance()) + "\n");
    }
    else
    {
        LogPrintStr("no wallet\n");
    }
}