#ifndef RANDOM_UTXO_GENERATOR_H
#define RANDOM_UTXO_GENERATOR_H
#include <amount.h>
#include <RandomCScriptGenerator.h>
#include <primitives/transaction.h>
#include <random.h>
class RandomUtxoGenerator
{
public:
    CTxOut operator()(CAmount maxAmount) const
    {
        RandomCScriptGenerator scriptGenerator;
        return CTxOut(GetRandHash().GetLow64(),scriptGenerator(25));
    }
};

#endif// RANDOM_UTXO_GENERATOR_H