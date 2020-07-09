#include <BlockIncentivesPopulator.h>

#include <string>
#include <base58.h>
#include <chainparams.h>
#include <SuperblockHelpers.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <BlockRewards.h>

#include <masternode-payments.h>

const std::string TREASURY_PAYMENT_ADDRESS("DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn");
const std::string CHARITY_PAYMENT_ADDRESS("DPujt2XAdHyRcZNB5ySZBBVKjzY2uXZGYq");

const std::string TREASURY_PAYMENT_ADDRESS_TESTNET("xw7G6toCcLr2J7ZK8zTfVRhAPiNc8AyxCd");
const std::string CHARITY_PAYMENT_ADDRESS_TESTNET("y8zytdJziDeXcdk48Wv7LH6FgnF4zDiXM5");

extern CChain chainActive;

CBitcoinAddress BlockIncentivesPopulator::TreasuryPaymentAddress()
{
    return CBitcoinAddress(Params().NetworkID() == CBaseChainParams::MAIN ? TREASURY_PAYMENT_ADDRESS : TREASURY_PAYMENT_ADDRESS_TESTNET);
}

CBitcoinAddress BlockIncentivesPopulator::CharityPaymentAddress()
{
    return CBitcoinAddress(Params().NetworkID() == CBaseChainParams::MAIN ? CHARITY_PAYMENT_ADDRESS : CHARITY_PAYMENT_ADDRESS_TESTNET);
}


void BlockIncentivesPopulator::FillTreasuryPayment(CMutableTransaction &tx, int nHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto rewards = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight);
    tx.vout.emplace_back(rewards.nTreasuryReward, GetScriptForDestination(TreasuryPaymentAddress().Get()));
    tx.vout.emplace_back(rewards.nCharityReward, GetScriptForDestination(CharityPaymentAddress().Get()));
}

void BlockIncentivesPopulator::FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex)
{
    auto lotteryWinners = currentBlockIndex->vLotteryWinnersCoinstakes;
    // when we call this we need to have exactly 11 winners

    auto nLotteryReward = rewards.nLotteryReward;
    auto nBigReward = nLotteryReward / 2;
    auto nSmallReward = nBigReward / 10;

    LogPrintf("%s : Paying lottery reward\n", __func__);
    for(size_t i = 0; i < lotteryWinners.size(); ++i) {
        CAmount reward = i == 0 ? nBigReward : nSmallReward;
        const auto &winner = lotteryWinners[i];
        LogPrintf("%s: Winner: %s\n", __func__, winner.first.ToString());
        auto scriptLotteryWinner = winner.second;
        tx.vout.emplace_back(reward, scriptLotteryWinner); // pay winners
    }
}

void BlockIncentivesPopulator::FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    SuperblockSubsidyContainer superblockSubsidies(Params());
    const I_SuperblockHeightValidator& heightValidator = superblockSubsidies.superblockHeightValidator();
    if (heightValidator.IsValidTreasuryBlockHeight(pindexPrev->nHeight + 1)) {
        FillTreasuryPayment(txNew, pindexPrev->nHeight + 1);
    }
    else if(heightValidator.IsValidLotteryBlockHeight(pindexPrev->nHeight + 1)) {
        FillLotteryPayment(txNew, payments, pindexPrev);
    }
    else {
        masternodePayments.FillBlockPayee(txNew, payments, fProofOfStake);
    }
}