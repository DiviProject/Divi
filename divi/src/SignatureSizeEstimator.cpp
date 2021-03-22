#include <SignatureSizeEstimator.h>
#include <keystore.h>
#include <script/script.h>
#include <wallet_ismine.h>
#include <script/standard.h>
#include <destination.h>

unsigned SignatureSizeEstimator::MaxBytesNeededForSigning(const CKeyStore& keystore,const CScript& scriptPubKey)
{
    return std::numeric_limits<unsigned>::max();
}