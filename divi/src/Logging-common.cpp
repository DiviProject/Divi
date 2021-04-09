#include <Logging.h>

#include "base58address.h"
#include "FeeRate.h"
#include "net.h"
#include "netbase.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"

LOG_FORMAT_WITH_TOSTRING(CAddress)
LOG_FORMAT_WITH_TOSTRING(CInv)
LOG_FORMAT_WITH_TOSTRING(CNetAddr)
LOG_FORMAT_WITH_TOSTRING(CService)

LOG_FORMAT_WITH_TOSTRING(CBitcoinAddress)
LOG_FORMAT_WITH_TOSTRING(CFeeRate)
LOG_FORMAT_WITH_TOSTRING(CKeyID)

LOG_FORMAT_WITH_TOSTRING(CBlock)
LOG_FORMAT_WITH_TOSTRING(CTxIn)
LOG_FORMAT_WITH_TOSTRING(CTxOut)
LOG_FORMAT_WITH_TOSTRING(CTransaction)

LOG_FORMAT_WITH_TOSTRING(CScript)
