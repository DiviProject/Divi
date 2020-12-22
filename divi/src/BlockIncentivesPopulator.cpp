#include <BlockIncentivesPopulator.h>

#include <string>
#include <base58.h>
#include <chainparams.h>
#include <SuperblockHelpers.h>
#include <primitives/transaction.h>
#include <chain.h>
#include <BlockRewards.h>

#include <masternode-payments.h>
#include <I_SuperblockHeightValidator.h>
#include <I_BlockSubsidyProvider.h>
#include <script/standard.h>
#include <Logging.h>

namespace
{
constexpr const char* TREASURY_PAYMENT_ADDRESS = "DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn";
constexpr const char* CHARITY_PAYMENT_ADDRESS = "DPujt2XAdHyRcZNB5ySZBBVKjzY2uXZGYq";

constexpr const char* TREASURY_PAYMENT_ADDRESS_TESTNET = "xw7G6toCcLr2J7ZK8zTfVRhAPiNc8AyxCd";
constexpr const char* CHARITY_PAYMENT_ADDRESS_TESTNET = "y8zytdJziDeXcdk48Wv7LH6FgnF4zDiXM5";

CBitcoinAddress TreasuryPaymentAddress(const CChainParams& chainParameters)
{
    return CBitcoinAddress(chainParameters.NetworkID() == CBaseChainParams::MAIN ? TREASURY_PAYMENT_ADDRESS : TREASURY_PAYMENT_ADDRESS_TESTNET);
}

CBitcoinAddress CharityPaymentAddress(const CChainParams& chainParameters)
{
    return CBitcoinAddress(chainParameters.NetworkID() == CBaseChainParams::MAIN ? CHARITY_PAYMENT_ADDRESS : CHARITY_PAYMENT_ADDRESS_TESTNET);
}

bool IsValidLotteryPayment(const CBlockRewards& rewards, const CTransaction &tx, const LotteryCoinstakes vRequiredWinnersCoinstake)
{
    if(vRequiredWinnersCoinstake.empty()) {
        return true;
    }

    auto verifyPayment = [&tx](CScript scriptPayment, CAmount amount) {
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    auto nLotteryReward = rewards.nLotteryReward;
    auto nBigReward = nLotteryReward / 2;
    auto nSmallReward = nBigReward / 10;

    for(size_t i = 0; i < vRequiredWinnersCoinstake.size(); ++i) {
        CScript scriptPayment = vRequiredWinnersCoinstake[i].second;
        CAmount reward = i == 0 ? nBigReward : nSmallReward;
        if(!verifyPayment(scriptPayment, reward)) {
            LogPrintf("%s: No payment for winner: %s\n", __func__, vRequiredWinnersCoinstake[i].first.ToString());
            return false;
        }
    }

    return true;
}

bool IsValidTreasuryPayment(const CChainParams& chainParameters, const CBlockRewards& rewards,const CTransaction &tx)
{
    auto charityPart = rewards.nCharityReward;
    auto treasuryPart = rewards.nTreasuryReward;

    auto verifyPayment = [&tx](CBitcoinAddress address, CAmount amount) {

        CScript scriptPayment = GetScriptForDestination(address.Get());
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    if(!verifyPayment(TreasuryPaymentAddress(chainParameters), treasuryPart))
    {
        LogPrint("masternode", "Expecting treasury payment, no payment address detected, rejecting\n");
        return false;
    }

    if(!verifyPayment(CharityPaymentAddress(chainParameters), charityPart))
    {
        LogPrint("masternode", "Expecting charity payment, no payment address detected, rejecting\n");
        return false;
    }

    return true;
}

} // anonymous namespace

bool CheckSuperblockPayees(
    const CChainParams& chainParameters,
    const I_SuperblockHeightValidator& heightValidator,
    const I_BlockSubsidyProvider& blockSubsidies,
    const CTransaction &txNew,
    const CBlockIndex* pindex)
{
    const CBlockRewards rewards = blockSubsidies.GetBlockSubsidity(pindex->nHeight);
    if(heightValidator.IsValidTreasuryBlockHeight(pindex->nHeight))
    {
        return IsValidTreasuryPayment(chainParameters,rewards,txNew);
    }
    if(heightValidator.IsValidLotteryBlockHeight(pindex->nHeight))
    {
        return IsValidLotteryPayment(rewards,txNew, pindex->pprev->vLotteryWinnersCoinstakes.getLotteryCoinstakes());
    }
    return true;
}

bool HasValidSuperblockPayees(const CChainParams& chainParameters, const SuperblockSubsidyContainer& superblockSubsidies, const CTransaction &txNew, const CBlockIndex* pindex)
{
    const I_SuperblockHeightValidator& heightValidator = superblockSubsidies.superblockHeightValidator();
    const I_BlockSubsidyProvider& blockSubsidies = superblockSubsidies.blockSubsidiesProvider();
    const unsigned blockHeight = pindex->nHeight;
    if(heightValidator.IsValidTreasuryBlockHeight(blockHeight) ||
        heightValidator.IsValidLotteryBlockHeight(blockHeight))
    {
        return CheckSuperblockPayees(chainParameters,heightValidator,blockSubsidies,txNew,pindex);
    }
    else
    {
        return true;
    }
}



const std::string TREASURY_PAYMENT_ADDRESS("DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn");
const std::string CHARITY_PAYMENT_ADDRESS("DPujt2XAdHyRcZNB5ySZBBVKjzY2uXZGYq");

const std::string TREASURY_PAYMENT_ADDRESS_TESTNET("xw7G6toCcLr2J7ZK8zTfVRhAPiNc8AyxCd");
const std::string CHARITY_PAYMENT_ADDRESS_TESTNET("y8zytdJziDeXcdk48Wv7LH6FgnF4zDiXM5");

BlockIncentivesPopulator::BlockIncentivesPopulator(
    const CChainParams& chainParameters,
    CChain& activeChain,
    CMasternodePayments& masternodePayments,
    const I_SuperblockHeightValidator& heightValidator,
    const I_BlockSubsidyProvider& blockSubsidies
    ): chainParameters_(chainParameters)
    , activeChain_(activeChain)
    , masternodePayments_(masternodePayments)
    , heightValidator_(heightValidator)
    , blockSubsidies_(blockSubsidies)
    , treasuryPaymentAddress_(
        chainParameters_.NetworkID() == CBaseChainParams::MAIN ? TREASURY_PAYMENT_ADDRESS : TREASURY_PAYMENT_ADDRESS_TESTNET)
    , charityPaymentAddress_(
        chainParameters_.NetworkID() == CBaseChainParams::MAIN ? CHARITY_PAYMENT_ADDRESS : CHARITY_PAYMENT_ADDRESS_TESTNET)
{
}

void BlockIncentivesPopulator::FillTreasuryPayment(CMutableTransaction &tx, int nHeight) const
{
    auto rewards = blockSubsidies_.GetBlockSubsidity(nHeight);
    tx.vout.emplace_back(rewards.nTreasuryReward, GetScriptForDestination( CBitcoinAddress(treasuryPaymentAddress_).Get()));
    tx.vout.emplace_back(rewards.nCharityReward, GetScriptForDestination( CBitcoinAddress(charityPaymentAddress_).Get()));
}

void BlockIncentivesPopulator::FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex) const
{
    auto lotteryWinners = currentBlockIndex->vLotteryWinnersCoinstakes.getLotteryCoinstakes();
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

void BlockIncentivesPopulator::FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, int newBlockHeight, bool fProofOfStake) const
{
    CBlockIndex* pindexPrev = activeChain_.Tip();
    if (!pindexPrev) return;

    if (heightValidator_.IsValidTreasuryBlockHeight(pindexPrev->nHeight + 1)) {
        FillTreasuryPayment(txNew, pindexPrev->nHeight + 1);
    }
    else if(heightValidator_.IsValidLotteryBlockHeight(pindexPrev->nHeight + 1)) {
        FillLotteryPayment(txNew, payments, pindexPrev);
    }
    else {
        masternodePayments_.FillBlockPayee(txNew, payments, fProofOfStake);
    }
}

bool IsBlockValueValid(const I_SuperblockHeightValidator& heightValidator, const CBlockRewards &nExpectedValue, CAmount nMinted, int nHeight)
{
    auto nExpectedMintCombined = nExpectedValue.nStakeReward + nExpectedValue.nMasternodeReward;

    // here we expect treasury block payment
    if(heightValidator.IsValidTreasuryBlockHeight(nHeight)) {
        nExpectedMintCombined += (nExpectedValue.nTreasuryReward + nExpectedValue.nCharityReward);
    }
    else if(heightValidator.IsValidLotteryBlockHeight(nHeight)) {
        nExpectedMintCombined += nExpectedValue.nLotteryReward;
    }

    if (nMinted > nExpectedMintCombined) {
        return false;
    }

    return true;
}