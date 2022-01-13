#ifndef RANDOM_SCRIPT_PUBKEY_GENERATOR_H
#define RANDOM_SCRIPT_PUBKEY_GENERATOR_H
#include <random.h>
#include <RandomCScriptGenerator.h>

enum ScriptType
{
    P2PKH,
    P2SH,
};
class RandomScriptPubKeyGenerator
{
public:
    CScript operator()(ScriptType type) const
    {
        RandomCScriptGenerator scriptGenerator;
        CScript script;
        switch (type)
        {
        case P2PKH:
            script << OP_DUP << OP_HASH160 << 0x14;
            script += scriptGenerator(0x14);
            script << OP_EQUALVERIFY << OP_CHECKSIG;
            assert(script.IsPayToPublicKeyHash());
            return script;
            break;
        case P2SH:
            script << OP_HASH160 << 0x14;
            script += scriptGenerator(0x14);
            script << OP_EQUAL;
            assert(script.IsPayToScriptHash());
            return script;
            break;
        default:
            assert(false && "unknown script type");
            break;
        }
    }
};
#endif// RANDOM_SCRIPT_PUBKEY_GENERATOR_H