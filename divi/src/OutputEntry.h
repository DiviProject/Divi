#ifndef OUTPUT_ENTRY_H
#define OUTPUT_ENTRY_H
#include <destination.h>
#include <amount.h>
class CWallet;
struct COutputEntry {
    CTxDestination destination;
    CAmount amount;
    int vout;
};
#endif// OUTPUT_ENTRY_H
