#ifndef RANDOM_CSCRIPT_H
#define RANDOM_CSCRIPT_H
#include <random>
#include <script/script.h>
class RandomCScriptGenerator
{
private:
    mutable std::mt19937 randomnessEngine_;
    mutable std::uniform_int_distribution<unsigned> distribution_;
public:
    RandomCScriptGenerator();
    CScript operator()(unsigned scriptLength) const;
};
#endif// RANDOM_CSCRIPT_H