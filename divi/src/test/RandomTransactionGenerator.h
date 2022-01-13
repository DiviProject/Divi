#ifndef RANDOM_TRANSACTION_GENERATOR_H
#define RANDOM_TRANSACTION_GENERATOR_H
#include <amount.h>
#include <RandomScriptPubKeyGenerator.h>
#include <primitives/transaction.h>
#include <random.h>
class RandomTransactionGenerator
{
public:
    CTransaction operator()(CAmount maxAmount = 1*COIN, unsigned inputCount = GetRandInt(15), unsigned outputCount = GetRandInt(15)) const
    {
        RandomScriptPubKeyGenerator scriptGenerator;
        CMutableTransaction tx;
        inputCount = std::max(inputCount,1u);
        outputCount = std::max(outputCount,1u);
        for(unsigned inputIndex = 0u; inputIndex < inputCount; ++inputIndex)
        {
            tx.vin.emplace_back(GetRandHash(), inputIndex);
        }
        for(unsigned outputIndex =0u; outputIndex < outputCount; ++outputIndex)
        {
            const bool useP2PKH = static_cast<bool>(GetRandInt(8) % 2);
            tx.vout.emplace_back(GetRand(maxAmount),scriptGenerator(useP2PKH?ScriptType::P2PKH:ScriptType::P2SH ));
        }
        return tx;
    }
};

#endif// RANDOM_TRANSACTION_GENERATOR_H