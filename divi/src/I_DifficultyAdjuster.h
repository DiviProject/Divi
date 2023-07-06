#ifndef I_DIFFICULTY_ADJUSTER_H
#define I_DIFFICULTY_ADJUSTER_H
class CBlockIndex;
class I_DifficultyAdjuster
{
public:
    virtual ~I_DifficultyAdjuster(){}
    virtual unsigned computeNextBlockDifficulty(const CBlockIndex* chainTip) const = 0;
};
#endif// I_DIFFICULTY_ADJUSTER_H