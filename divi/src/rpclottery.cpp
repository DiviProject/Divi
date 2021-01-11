#include <chain.h>
#include <spork.h>
#include <LotteryWinnersCalculator.h>
#include <SuperblockSubsidyContainer.h>
#include <script/standard.h>
#include <json/json_spirit_value.h>
#include <chainparams.h>
#include <base58address.h>
#include <rpcprotocol.h>
#include <sync.h>

extern CChain chainActive;
extern CCriticalSection cs_main;
using namespace json_spirit;

Value getlotteryblockwinners(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw std::runtime_error(
            "getlotteryblockwinners [block_height]\n"
            "\nArguments:\n"
            "1. block_height         (numeric, optional) The block at which to query the list of winners\n"
            "\nReturns a json object with the list of current candidates to win the lottery since the last known block.\n"
            "\nExample:\n"
            "{\n"
            "    'Block Height': 189,\n"
            "    'Block Hash ': 'cfa03ee7d2fa2ebcb822d791404c21357e47e1976afa92977a5dc118533527c5',\n"
            "    'Lottery Candidates': [\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 0, 'Score': 'ebca7a39'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 1, 'Score': 'a76c2a59'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 2, 'Score': '5fe24590'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 3, 'Score': '5cab25ce'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 4, 'Score': '57bfaf91'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 5, 'Score': '535158e8'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 6, 'Score': '1f05a975'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 7, 'Score': '090172bc'},\n"
            "        {'Address': 'yE7D1Ne4nfXnX381eR2gZtW7PW2vFFQ627', 'Rank': 8, 'Score': '03810747'}\n"
            "    ]\n"
            "}\n");



    static const CChainParams& chainParameters = Params();
    static SuperblockSubsidyContainer subsidyCointainer(chainParameters);
    static LotteryWinnersCalculator calculator(
        chainParameters.GetLotteryBlockStartBlock(),chainActive, sporkManager,subsidyCointainer.superblockHeightValidator());
    const CBlockIndex* chainTip = nullptr;
    {
        LOCK(cs_main);
        chainTip = chainActive.Tip();
        if(!chainTip) throw JSONRPCError(RPC_MISC_ERROR,"Could not acquire lock on chain tip.");
    }
    int blockHeight = (params.size()>0)? params[0].get_int(): chainTip->nHeight;
    const CBlockIndex* soughtIndex = chainTip->GetAncestor(blockHeight);

    const LotteryCoinstakes& coinstakesAtChainTip = soughtIndex->vLotteryWinnersCoinstakes.getLotteryCoinstakes();
    RankedScoreAwareCoinstakes lotteryCurrentResults =
        calculator.computeRankedScoreAwareCoinstakes(
            calculator.GetLastLotteryBlockIndexBeforeHeight(soughtIndex->nHeight)->GetBlockHash(),
            coinstakesAtChainTip);

    Object result;
    result.push_back(Pair("Block Height",soughtIndex->nHeight));
    result.push_back(Pair("Block Hash ",soughtIndex->GetBlockHash().ToString()));
    Array lotteryResults;
    for(const LotteryCoinstake& coinstake: coinstakesAtChainTip)
    {
        Object coinstakeResult;
        const auto& rankAwareEntry = lotteryCurrentResults[coinstake.first];

        txnouttype outputType;
        std::vector<CTxDestination> addresses;
        int requiredSigs;
        ExtractDestinations(coinstake.second, outputType, addresses, requiredSigs);
        std::string destinationString = "";
        for(const CTxDestination& dest: addresses)
        {
            destinationString += CBitcoinAddress(dest).ToString();
            destinationString += ":";
        }
        destinationString = destinationString.substr(0, destinationString.size()-1);

        coinstakeResult.push_back(Pair("Address", destinationString));
        coinstakeResult.push_back(Pair("Rank", static_cast<uint64_t>(rankAwareEntry.rank) ));
        coinstakeResult.push_back(Pair("Score", rankAwareEntry.score.ToString().substr(0,8) ));

        lotteryResults.push_back(coinstakeResult);
    }
    result.push_back(Pair("Lottery Candidates",lotteryResults));
    return result;
}