#ifndef I_MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H
#define I_MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H
class CValidationState;
class I_MostWorkChainTipLocator
{
public:
    virtual ~I_MostWorkChainTipLocator(){}
    virtual CBlockIndex* findMostWorkChain() const = 0;
};

class I_MostWorkChainTransitionMediator: public I_MostWorkChainTipLocator
{
public:
    virtual ~I_MostWorkChainTransitionMediator(){}
    virtual bool transitionActiveChainToMostWorkChain(
            CValidationState& state,
            CBlockIndex* pindexMostWork,
            const CBlock* pblock) const = 0;
};

#endif// I_MOST_WORK_CHAIN_TRANSITION_MEDIATOR_H