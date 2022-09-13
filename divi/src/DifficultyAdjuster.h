#ifndef DIFFICULTY_ADJUSTER_H
#define DIFFICULTY_ADJUSTER_H
#include <I_DifficultyAdjuster.h>
class CChainParams;
class DifficultyAdjuster final: public I_DifficultyAdjuster
{
private:
    const CChainParams& chainParameters_;
public:
    DifficultyAdjuster(const CChainParams& chainParameters);
    unsigned computeNextBlockDifficulty(const CBlockIndex* chainTip) const override;
};
#endif// DIFFICULTY_ADJUSTER_H