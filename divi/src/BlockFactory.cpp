#include <BlockFactory.h>
#include <script.h>
#include <wallet.h>
#include <BlockTemplate.h>

CBlockTemplate* BlockFactory::createNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake)
{
    return nullptr;
}

CBlockTemplate* BlockFactory::createNewBlockWithKey(CReserveKey& reservekey, bool fProofOfStake)
{
    CPubKey pubkey;
    if (!reservekey.GetReservedKey(pubkey, false))
        return NULL;

    CScript scriptPubKey = CScript() << ToByteVector(pubkey) << OP_CHECKSIG;
    return CreateNewBlock(scriptPubKey, pwallet_, fProofOfStake);
}