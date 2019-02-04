// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <masternodes/masternode-payments.h>
#include <addrman.h>
#include <chainparamsbase.h>
#include <consensus/validation.h>
#include <masternodes/activemasternode.h>
#include <masternodes/masternode-sync.h>
#include <masternodes/masternodeman.h>
#include <spork.h>
#include <sync.h>
#include <netmessagemaker.h>
#include <netfulfilledman.h>
#include <key_io.h>
#include <messagesigner.h>
#include <util/moneystr.h>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <numeric>

/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMasternodeBlocks;
CCriticalSection cs_mapMasternodePayeeVotes;

const std::string TREASURY_PAYMENT_ADDRESS("DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn");
const std::string CHARITY_PAYMENT_ADDRESS("DPujt2XAdHyRcZNB5ySZBBVKjzY2uXZGYq");

const std::string TREASURY_PAYMENT_ADDRESS_TESTNET("xw7G6toCcLr2J7ZK8zTfVRhAPiNc8AyxCd");
const std::string CHARITY_PAYMENT_ADDRESS_TESTNET("y8zytdJziDeXcdk48Wv7LH6FgnF4zDiXM5");

bool IsValidLotteryBlockHeight(int nBlockHeight, const Consensus::Params &consensus)
{
    return nBlockHeight >= consensus.nLotteryBlockStartBlock &&
            ((nBlockHeight % consensus.nLotteryBlockCycle) == 0);
}

static bool IsValidTreasuryBlockHeight(int nBlockHeight, const Consensus::Params &consensus)
{
    return nBlockHeight >= consensus.nTreasuryPaymentsStartBlock &&
            ((nBlockHeight % consensus.nTreasuryPaymentsCycle) == 0);
}

static int64_t GetTreasuryReward(const CBlockRewards &rewards, const Consensus::Params &consensus)
{
    return rewards.nTreasuryReward * consensus.nTreasuryPaymentsCycle;
}

static int64_t GetCharityReward(const CBlockRewards &rewards, const Consensus::Params &consensus)
{
    return rewards.nCharityReward * consensus.nTreasuryPaymentsCycle;
}

static CTxDestination TreasuryPaymentAddress()
{
    return DecodeDestination(Params().NetworkIDString() == CBaseChainParams::MAIN ? TREASURY_PAYMENT_ADDRESS : TREASURY_PAYMENT_ADDRESS_TESTNET);
}

static CTxDestination CharityPaymentAddress()
{
    return DecodeDestination(Params().NetworkIDString() == CBaseChainParams::MAIN ? CHARITY_PAYMENT_ADDRESS : CHARITY_PAYMENT_ADDRESS_TESTNET);
}


static void FillTreasuryPayment(CMutableTransaction &tx, int nHeight, const Consensus::Params &consensus)
{
    auto rewards = GetBlockSubsidity(nHeight - 1, consensus);
    tx.vout.emplace_back(GetTreasuryReward(rewards, consensus), GetScriptForDestination(TreasuryPaymentAddress()));
    tx.vout.emplace_back(GetCharityReward(rewards, consensus), GetScriptForDestination(CharityPaymentAddress()));
}

static int64_t GetLotteryReward(const CBlockRewards &rewards, const Consensus::Params &consensus)
{
    // 50 coins every block for lottery
    return consensus.nLotteryBlockCycle * rewards.nLotteryReward;
}

static CScript GetScriptForLotteryPayment(const uint256 &hashWinningCoinstake, const Consensus::Params &consensus)
{
    CTransactionRef coinbaseTx;
    uint256 hashBlock;
    assert(GetTransaction(hashWinningCoinstake, coinbaseTx, consensus, hashBlock, true));
    assert(coinbaseTx->IsCoinBase() || coinbaseTx->IsCoinStake());

    if(coinbaseTx->IsCoinBase())
    {
        return coinbaseTx->vout[0].scriptPubKey;
    }
    else
    {
        return coinbaseTx->vout[1].scriptPubKey;
    }
}

static void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex, const Consensus::Params &consensus)
{
    auto lotteryWinners = currentBlockIndex->vLotteryWinnersCoinstakes;
    // when we call this we need to have exactly 11 winners

    auto nLotteryReward = GetLotteryReward(rewards, consensus);
    auto nBigReward = nLotteryReward / 2;
    auto nSmallReward = nBigReward / 10;

    LogPrintf("%s : Paying lottery reward\n", __func__);
    for(size_t i = 0; i < lotteryWinners.size(); ++i) {
        CAmount reward = i == 0 ? nBigReward : nSmallReward;
        const auto &winner = lotteryWinners[i];
        LogPrintf("%s: Winner: %s\n", __func__, winner.ToString());
        auto scriptLotteryWinner = GetScriptForLotteryPayment(winner, consensus);
        tx.vout.emplace_back(reward, scriptLotteryWinner); // pay winners
    }
}

static bool IsValidLotteryPayment(const CTransaction &tx, int nHeight, const std::vector<WinnerCoinStake> vRequiredWinnersCoinstake, const Consensus::Params &consensus)
{
    if(vRequiredWinnersCoinstake.empty()) {
        return true;
    }

    auto verifyPayment = [&tx](CScript scriptPayment, CAmount amount) {
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    auto nLotteryReward = GetLotteryReward(GetBlockSubsidity(nHeight, consensus), consensus);
    auto nBigReward = nLotteryReward / 2;
    auto nSmallReward = nBigReward / 10;

    for(size_t i = 0; i < vRequiredWinnersCoinstake.size(); ++i) {
        CScript scriptPayment = GetScriptForLotteryPayment(vRequiredWinnersCoinstake[i], consensus);
        CAmount reward = i == 0 ? nBigReward : nSmallReward;
        if(!verifyPayment(scriptPayment, reward)) {
            LogPrintf("%s: No payment for winner: %s\n", vRequiredWinnersCoinstake[i].ToString());
            return false;
        }
    }

    return true;
}

static bool IsValidTreasuryPayment(const CTransaction &tx, int nHeight, const Consensus::Params &consensus)
{
    auto rewards = GetBlockSubsidity(nHeight, consensus);
    auto charityPart = GetCharityReward(rewards, consensus);
    auto treasuryPart = GetTreasuryReward(rewards, consensus);

    auto verifyPayment = [&tx](CTxDestination address, CAmount amount) {

        CScript scriptPayment = GetScriptForDestination(address);
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    if(!verifyPayment(TreasuryPaymentAddress(), treasuryPart))
    {
        LogPrint(BCLog::MASTERNODE, "Expecting treasury payment, no payment address detected, rejecting\n");
        return false;
    }

    if(!verifyPayment(CharityPaymentAddress(), charityPart))
    {
        LogPrint(BCLog::MASTERNODE, "Expecting charity payment, no payment address detected, rejecting\n");
        return false;
    }

    return true;
}

bool IsBlockValueValid(const CBlock& block, const CBlockRewards &nExpectedValue, CAmount nMinted, const Consensus::Params &consensus)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev == NULL) return true;

    int nHeight = 0;
    if (pindexPrev->GetBlockHash() == block.hashPrevBlock) {
        nHeight = pindexPrev->nHeight + 1;
    } else { //out of order
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second)
            nHeight = (*mi).second->nHeight + 1;
    }

    if (nHeight == 0) {
        LogPrint(BCLog::MASTERNODE,"IsBlockValueValid() : WARNING: Couldn't find previous block\n");
    }

    //LogPrintf("XX69----------> IsBlockValueValid(): nMinted: %d, nExpectedValue: %d\n", FormatMoney(nMinted), FormatMoney(nExpectedValue));

    auto nExpectedMintCombined = nExpectedValue.nStakeReward + nExpectedValue.nMasternodeReward;

    // here we expect treasury block payment
    if(IsValidTreasuryBlockHeight(nHeight, consensus)) {
        nExpectedMintCombined += (GetTreasuryReward(nExpectedValue, consensus) + GetCharityReward(nExpectedValue, consensus));
    }
    else if(IsValidLotteryBlockHeight(nHeight, consensus)) {
        nExpectedMintCombined += GetLotteryReward(nExpectedValue, consensus);
    }

    if (nMinted > nExpectedMintCombined) {
        return false;
    }

    return true;
}

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CBlockIndex *prevIndex, const Consensus::Params &consensus)
{
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrintf("%s : Client not synced, skipping block payee checks\n", __func__);
        return true;
    }

    if(IsValidTreasuryBlockHeight(nBlockHeight, consensus)) {
        return IsValidTreasuryPayment(txNew, nBlockHeight, consensus);
    }

    if(IsValidLotteryBlockHeight(nBlockHeight, consensus)) {
        return IsValidLotteryPayment(txNew, nBlockHeight, prevIndex->vLotteryWinnersCoinstakes, consensus);
    }

    //check for masternode payee
    if (masternodePayments.IsTransactionValid(txNew, nBlockHeight, consensus))
        return true;
    LogPrintf("%s : Invalid mn payment detected %s\n", __func__, txNew.ToString().c_str());

    if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrintf("%s : Masternode payment enforcement is disabled, accepting block\n", __func__);

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake, const Consensus::Params &consensus)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    if (IsValidTreasuryBlockHeight(pindexPrev->nHeight + 1, consensus)) {
        FillTreasuryPayment(txNew, pindexPrev->nHeight + 1, consensus);
    }
    else if(IsValidLotteryBlockHeight(pindexPrev->nHeight + 1, consensus)) {
        FillLotteryPayment(txNew, payments, pindexPrev, consensus);
    }
    else {
        masternodePayments.FillBlockPayee(txNew, payments, fProofOfStake, consensus);
    }
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake, const Consensus::Params &consensus)
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, payee)) {
        //no masternode detected
        CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint(BCLog::MASTERNODE,"CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    if (hasPayment) {
        CAmount masternodePayment = rewards.nMasternodeReward;
        txNew.vout.emplace_back(masternodePayment, payee);

        CTxDestination address1;
        ExtractDestination(payee, address1);

        LogPrint(BCLog::MASTERNODE, "Masternode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), EncodeDestination(address1));
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
    //    if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
    return MIN_PEER_PROTO_VERSION;
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, CValidationState &state, const string &strCommand, CDataStream& vRecv, CConnman &connman)
{
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MASTERNODEPAYMENTSYNC) { //Masternode Payments Request Sync
        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC)) {
                LogPrintf("%s : mnget - peer already asked me for the list\n", __func__);
                state.DoS(20, false, REJECT_INVALID, "mnget - peer already asked me for the list");
                return;
            }
        }

        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::MASTERNODEPAYMENTSYNC);
        masternodePayments.Sync(pfrom, nCountNeeded, connman);
        LogPrint(BCLog::MNPAYMENTS, "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == NetMsgType::MASTERNODEPAYMENTVOTE) { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < MIN_PEER_PROTO_VERSION) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrint(BCLog::MNPAYMENTS, "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint(BCLog::MNPAYMENTS, "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError, connman)) {
            // if(strError != "") LogPrint(BCLog::MASTERNODE,"mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight)) {
            //  LogPrint(BCLog::MASTERNODE,"mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            LogPrintf("%s : - invalid signature\n", __func__);
            if (masternodeSync.IsSynced())
            {
                state.DoS(20, false, REJECT_INVALID, "mnget - peer already asked me for the list");
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinMasternode, connman);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);

        //   LogPrint(BCLog::MNPAYMENTS, "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay(connman);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePaymentWinner::Sign(CKey& keyMasternode, CPubKey& pubKeyMasternode)
{
    std::string errorMessage;
    std::string strMasterNodeSignMessage;

    std::string strMessage = vinMasternode.prevout.ToStringShort() +
            boost::lexical_cast<std::string>(nBlockHeight) +
            payee.ToString();

    if (!CMessageSigner::SignMessage(strMessage, vchSig, keyMasternode, CPubKey::InputScriptType::SPENDP2PKH)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!CMessageSigner::VerifyMessage(pubKeyMasternode.GetID(), vchSig, strMessage, errorMessage)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    return true;
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(const CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew, const Consensus::Params &consensus)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;

    std::string strPayeesPossible = "";

    auto rewards = GetBlockSubsidity(nBlockHeight, consensus);

    CAmount requiredMasternodePayment = rewards.nMasternodeReward;

    //require at least 6 signatures
    for(CMasternodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (CMasternodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue >= requiredMasternodePayment)
                    found = true;
                else
                    LogPrint(BCLog::MASTERNODE,"Masternode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += EncodeDestination(address1);
            } else {
                strPayeesPossible += "," + EncodeDestination(address1);
            }
        }
    }

    LogPrint(BCLog::MASTERNODE,"CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CMasternodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);

        if (ret != "Unknown") {
            ret += ", " + EncodeDestination(address1) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = EncodeDestination(address1) + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        }
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight, const Consensus::Params &consensus)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew, consensus);
    }

    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint(BCLog::MNPAYMENTS, "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError, CConnman &connman)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.hash.ToString());
        LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode, connman);
        return false;
    }

    if (pmn->protocolVersion < MIN_PEER_PROTO_VERSION) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, MIN_PEER_PROTO_VERSION);
        LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, MIN_PEER_PROTO_VERSION);

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight, CConnman &connman)
{
    if (!fMasterNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100, MIN_PEER_PROTO_VERSION);

    if (n == -1) {
        LogPrint(BCLog::MNPAYMENTS, "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint(BCLog::MNPAYMENTS, "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin);

    LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn != NULL) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.nBlockHeight = nBlockHeight;

        CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(payee);

        CTxDestination address1;
        ExtractDestination(payee, address1);

        LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(address1), newWinner.nBlockHeight);
    } else {
        LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
    }

    std::string errorMessage;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!CMessageSigner::SetKey(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrint(BCLog::MASTERNODE,"CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

        if (AddWinningMasternode(newWinner)) {
            newWinner.Relay(connman);
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CMasternodePaymentWinner::Relay(CConnman &connman)
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    connman.RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid()
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn != NULL) {
        std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payee.ToString();

        std::string errorMessage = "";
        if (!CMessageSigner::VerifyMessage(pmn->pubKeyMasternode.GetID(), vchSig, strMessage, errorMessage)) {
            return error("CMasternodePaymentWinner::SignatureValid() - Got bad Masternode address signature %s\n", vinMasternode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded, CConnman &connman)
{
    LOCK(cs_mapMasternodePayeeVotes);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    connman.PushMessage(node, CNetMsgMaker(node->GetRecvVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_MNW, nInvCount));
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}


int CMasternodePayments::GetOldestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nOldestBlock = std::numeric_limits<int>::max();

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first < nOldestBlock) {
            nOldestBlock = (*it).first;
        }
        it++;
    }

    return nOldestBlock;
}


int CMasternodePayments::GetNewestBlock()
{
    LOCK(cs_mapMasternodeBlocks);

    int nNewestBlock = 0;

    std::map<int, CMasternodeBlockPayees>::iterator it = mapMasternodeBlocks.begin();
    while (it != mapMasternodeBlocks.end()) {
        if ((*it).first > nNewestBlock) {
            nNewestBlock = (*it).first;
        }
        it++;
    }

    return nNewestBlock;
}

static arith_uint256 CalculateLotteryScore(const uint256 &hashCoinbaseTx, const uint256 &hashLastLotteryBlock)
{
    // Deterministically calculate a "score" for a Masternode based on any given (block)hash
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << hashCoinbaseTx << hashLastLotteryBlock;
    return UintToArith256(ss.GetHash());
}

static bool IsCoinstakeValidForLottery(const CTransaction &tx, int nHeight)
{
    CAmount nAmount = 0;
    if(tx.IsCoinBase()) {
        nAmount = tx.vout[0].nValue;
    }
    else {
        auto payee = tx.vout[1].scriptPubKey;
        nAmount = std::accumulate(std::begin(tx.vout), std::end(tx.vout), CAmount(0), [payee](CAmount accum, const CTxOut &out) {
                return out.scriptPubKey == payee ? accum + out.nValue : accum;
    });
    }

    int nMinStakeValue = 10000; // default is 10k

    if(sporkManager.IsSporkActive(SPORK_16_LOTTERY_TICKET_MIN_VALUE)) {
        MultiValueSporkList<LotteryTicketMinValueSporkValue> vValues;
        CSporkManager::ConvertMultiValueSporkVector(sporkManager.GetMultiValueSpork(SPORK_16_LOTTERY_TICKET_MIN_VALUE), vValues);
        auto nBlockTime = chainActive[nHeight] ? chainActive[nHeight]->nTime : GetAdjustedTime();
        LotteryTicketMinValueSporkValue activeSpork = CSporkManager::GetActiveMultiValueSpork(vValues, nHeight, nBlockTime);

        if(activeSpork.IsValid()) {
            // we expect that this value is in coins, not in satoshis
            nMinStakeValue = activeSpork.nEntryTicketValue;
        }
    }

    return nAmount > nMinStakeValue * COIN; // only if stake is more than 10k
}

std::vector<WinnerCoinStake> CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight, const Consensus::Params &consensus)
{
    std::vector<WinnerCoinStake> result;
    // if that's a block when lottery happens, reset score for whole cycle
    if(IsValidLotteryBlockHeight(nHeight, consensus))
        return result;

    if(!prevBlockIndex)
        return result;

    int nLastLotteryHeight = std::max(consensus.nLotteryBlockStartBlock, consensus.nLotteryBlockCycle * ((nHeight - 1) / consensus.nLotteryBlockCycle));

    if(nHeight <= nLastLotteryHeight) {
        return result;
    }

    const auto& coinbaseTx = (nHeight > consensus.nLastPOWBlock ? block.vtx[1] : block.vtx[0]);

    if(!IsCoinstakeValidForLottery(*coinbaseTx, nHeight)) {
        return prevBlockIndex->vLotteryWinnersCoinstakes; // return last if we have no lotter participant in this block
    }

    CBlockIndex* pblockindex = chainActive[nLastLotteryHeight];
    auto hashLastLotteryBlock = pblockindex->GetBlockHash();
    // lotteryWinnersCoinstakes has hashes of coinstakes, let calculate old scores + new score
    using LotteryScore = arith_uint256;
    std::vector<std::pair<LotteryScore, WinnerCoinStake>> scores;
    for(auto &&hashCoinstake : prevBlockIndex->vLotteryWinnersCoinstakes) {
        scores.emplace_back(CalculateLotteryScore(hashCoinstake, hashLastLotteryBlock), hashCoinstake);
    }

    auto newScore = CalculateLotteryScore(coinbaseTx->GetHash(), hashLastLotteryBlock);
    scores.emplace_back(newScore, coinbaseTx->GetHash());

    // biggest entry at the begining
    if(scores.size() > 1)
    {
        std::sort(std::begin(scores), std::end(scores), [](const std::pair<LotteryScore, WinnerCoinStake> &lhs, const std::pair<LotteryScore, WinnerCoinStake> &rhs) {
            return lhs.first > rhs.first;
        });
    }

    scores.resize(std::min<size_t>(scores.size(), 11)); // don't go over 11 entries, since we will have only 11 winners

    // prepare new coinstakes vector
    for(auto &&score : scores) {
        result.push_back(score.second);
    }

    return result;
}


//TODO: Rename/move to core
void ThreadCheckObfuScationPool(CWallet &wallet, CConnman &connman)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("divi-obfuscation");

    unsigned int c = 0;

    while (true) {
        MilliSleep(1000);
        //LogPrintf("ThreadCheckObfuScationPool::check timeout\n");

        // try to sync from all available nodes, one step at a time
        masternodeSync.Process(connman);

        if (masternodeSync.IsBlockchainSynced()) {
            c++;

            // check if we should activate or ping every few minutes,
            // start right after sync is considered to be done
            if (c % MASTERNODE_PING_SECONDS == 1) activeMasternode.ManageStatus(wallet, connman);

            if (c % 60 == 0) {
                mnodeman.CheckAndRemoveInnactive();
                mnodeman.ProcessMasternodeConnections();
                masternodePayments.CheckAndRemove();
//                CleanTransactionLocksList();
            }

            //if(c % MASTERNODES_DUMP_SECONDS == 0) DumpMasternodes();

//            obfuScationPool.CheckTimeout();
//            obfuScationPool.CheckForCompleteQueue();

//            if (obfuScationPool.GetState() == POOL_STATUS_IDLE && c % 15 == 0) {
//                obfuScationPool.DoAutomaticDenominating();
//            }
        }
    }
}
