// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"
#include "addrman.h"
#include "BlockDiskAccessor.h"
#include "chainparamsbase.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "spork.h"
#include "sync.h"
#include "Logging.h"
#include "utilmoneystr.h"
#include "netfulfilledman.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <numeric>

#include <SuperblockHelpers.h>
#include <LotteryWinnersCalculator.h>

#define MNPAYMENTS_SIGNATURES_REQUIRED 6
#define MNPAYMENTS_SIGNATURES_TOTAL 10
/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

CCriticalSection cs_vecPayments;
CCriticalSection cs_mapMasternodeBlocks;
CCriticalSection cs_mapMasternodePayeeVotes;

const std::string TREASURY_PAYMENT_ADDRESS("DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn");
const std::string CHARITY_PAYMENT_ADDRESS("DPujt2XAdHyRcZNB5ySZBBVKjzY2uXZGYq");

const std::string TREASURY_PAYMENT_ADDRESS_TESTNET("xw7G6toCcLr2J7ZK8zTfVRhAPiNc8AyxCd");
const std::string CHARITY_PAYMENT_ADDRESS_TESTNET("y8zytdJziDeXcdk48Wv7LH6FgnF4zDiXM5");


static CBitcoinAddress TreasuryPaymentAddress()
{
    return CBitcoinAddress(Params().NetworkID() == CBaseChainParams::MAIN ? TREASURY_PAYMENT_ADDRESS : TREASURY_PAYMENT_ADDRESS_TESTNET);
}

static CBitcoinAddress CharityPaymentAddress()
{
    return CBitcoinAddress(Params().NetworkID() == CBaseChainParams::MAIN ? CHARITY_PAYMENT_ADDRESS : CHARITY_PAYMENT_ADDRESS_TESTNET);
}


static void FillTreasuryPayment(CMutableTransaction &tx, int nHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto rewards = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight);
    tx.vout.emplace_back(rewards.nTreasuryReward, GetScriptForDestination(TreasuryPaymentAddress().Get()));
    tx.vout.emplace_back(rewards.nCharityReward, GetScriptForDestination(CharityPaymentAddress().Get()));
}

static void FillLotteryPayment(CMutableTransaction &tx, const CBlockRewards &rewards, const CBlockIndex *currentBlockIndex)
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

void FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &payments, bool fProofOfStake)
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

static bool IsValidLotteryPayment(const CTransaction &tx, int nHeight, const LotteryCoinstakes vRequiredWinnersCoinstake)
{
    if(vRequiredWinnersCoinstake.empty()) {
        return true;
    }

    auto verifyPayment = [&tx](CScript scriptPayment, CAmount amount) {
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto nLotteryReward = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight).nLotteryReward;
    auto nBigReward = nLotteryReward / 2;
    auto nSmallReward = nBigReward / 10;

    for(size_t i = 0; i < vRequiredWinnersCoinstake.size(); ++i) {
        CScript scriptPayment = vRequiredWinnersCoinstake[i].second;
        CAmount reward = i == 0 ? nBigReward : nSmallReward;
        if(!verifyPayment(scriptPayment, reward)) {
            LogPrintf("%s: No payment for winner: %s\n", vRequiredWinnersCoinstake[i].first.ToString());
            return false;
        }
    }

    return true;
}

static bool IsValidTreasuryPayment(const CTransaction &tx, int nHeight)
{
    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto rewards = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nHeight);
    auto charityPart = rewards.nCharityReward;
    auto treasuryPart = rewards.nTreasuryReward;

    auto verifyPayment = [&tx](CBitcoinAddress address, CAmount amount) {

        CScript scriptPayment = GetScriptForDestination(address.Get());
        CTxOut outPayment(amount, scriptPayment);
        return std::find(std::begin(tx.vout), std::end(tx.vout), outPayment) != std::end(tx.vout);
    };

    if(!verifyPayment(TreasuryPaymentAddress(), treasuryPart))
    {
        LogPrint("masternode", "Expecting treasury payment, no payment address detected, rejecting\n");
        return false;
    }

    if(!verifyPayment(CharityPaymentAddress(), charityPart))
    {
        LogPrint("masternode", "Expecting charity payment, no payment address detected, rejecting\n");
        return false;
    }

    return true;
}

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CBlockIndex *prevIndex)
{
    SuperblockSubsidyContainer superblockSubsidies(Params());
    const I_SuperblockHeightValidator& heightValidator = superblockSubsidies.superblockHeightValidator();
    if(heightValidator.IsValidTreasuryBlockHeight(nBlockHeight)) {
        return IsValidTreasuryPayment(txNew, nBlockHeight);
    }

    if(heightValidator.IsValidLotteryBlockHeight(nBlockHeight)) {
        return IsValidLotteryPayment(txNew, nBlockHeight, prevIndex->vLotteryWinnersCoinstakes);
    }
    
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrintf("%s : Client not synced, skipping block payee checks\n", __func__);
        return true;
    }
    //check for masternode payee
    if (masternodePayments.IsTransactionValid(txNew, nBlockHeight))
        return true;
    LogPrintf("%s : Invalid mn payment detected %s\n", __func__, txNew.ToString().c_str());

    if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrintf("%s : Masternode payment enforcement is disabled, accepting block\n", __func__);

    return true;
}

LotteryCoinstakes CalculateLotteryWinners(const CBlock &block, const CBlockIndex *prevBlockIndex, int nHeight)
{
    const CChainParams& chainParameters = Params();
    SuperblockSubsidyContainer subsidyCointainer(chainParameters);
    return LotteryWinnersCalculator(chainParameters,chainActive, sporkManager,subsidyCointainer.superblockHeightValidator()).CalculateLotteryWinners(block,prevBlockIndex,nHeight);
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake)
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
            LogPrint("masternode","CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    if (hasPayment) {
        CAmount masternodePayment = rewards.nMasternodeReward;
        txNew.vout.emplace_back(masternodePayment, payee);

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("masternode","Masternode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), address2.ToString().c_str());
    }
}

int CMasternodePayments::GetMinMasternodePaymentsProto()
{
    //    if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
    return ActiveProtocol();                          // Allow only updated peers
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Obfuscation/Masternode related functionality


    if (strCommand == "mnget") { //Masternode Payments Request Sync
        if (fLiteMode) return;   //disable all Obfuscation/Masternode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, "mnget")) 
        {
            LogPrintf("%s : mnget - peer already asked me for the list\n", __func__);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }

        netfulfilledman.AddFulfilledRequest(pfrom->addr, "mnget");
        masternodePayments.Sync(pfrom, nCountNeeded);
        LogPrint("mnpayments", "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == "mnw") { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("masternode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight)) {
            //  LogPrint("masternode","mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!winner.SignatureValid()) {
            LogPrintf("%s : - invalid signature\n", __func__);
            if (masternodeSync.IsSynced()) Misbehaving(pfrom->GetId(), 20);
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinMasternode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);
        CBitcoinAddress address2(address1);

        //   LogPrint("mnpayments", "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", address2.ToString().c_str(), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay();
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

    if (!CObfuScationSigner::SignMessage(strMessage, errorMessage, vchSig, keyMasternode)) {
        LogPrint("masternode","CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
        return false;
    }

    if (!CObfuScationSigner::VerifyMessage(pubKeyMasternode, vchSig, strMessage, errorMessage)) {
        LogPrint("masternode","CMasternodePing::Sign() - Error: %s\n", errorMessage.c_str());
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
    uint256 blockHash = 0;
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

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew)
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;

    std::string strPayeesPossible = "";

    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto rewards = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nBlockHeight);

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
                    LogPrint("masternode","Masternode payment is out of drift range. Paid=%s Min=%s\n", FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible += address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    LogPrint("masternode","CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    BOOST_FOREACH (CMasternodePayee& payee, vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        CBitcoinAddress address2(address1);

        if (ret != "Unknown") {
            ret += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
        } else {
            ret = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.nVotes);
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

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew);
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
            LogPrint("mnpayments", "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.hash.ToString());
        LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    if(!masternodeSync.IsSynced()){ return true;}

    std::vector<CMasternode*> mnQueue = mnodeman.GetMasternodePaymentQueue(nBlockHeight,true);
    std::vector<CMasternode*>::iterator it = std::find(mnQueue.begin(),mnQueue.end(), mnodeman.Find(payee));
    if(it != mnQueue.end())
    {
        return (std::distance(mnQueue.begin(),it) < 2*MNPAYMENTS_SIGNATURES_TOTAL)? true : false;
    }
    else
    {
        return false;
    }
}

bool CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return false;

    //reference node - hybrid mode

    int n = mnodeman.GetMasternodeRank(activeMasternode.vin, nBlockHeight - 100, ActiveProtocol());

    if (n == -1) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin);

    LogPrint("masternode","CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true);

    if (pmn != NULL) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        newWinner.nBlockHeight = nBlockHeight;

        CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(payee);

        CTxDestination address1;
        ExtractDestination(payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("masternode","CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address2.ToString().c_str(), newWinner.nBlockHeight);
    } else {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
    }

    std::string errorMessage;
    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!CObfuScationSigner::SetKey(strMasterNodePrivKey, errorMessage, keyMasternode, pubKeyMasternode)) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() - Error upon calling SetKey: %s\n", errorMessage.c_str());
        return false;
    }

    LogPrint("masternode","CMasternodePayments::ProcessBlock() - Signing Winner\n");
    if (newWinner.Sign(keyMasternode, pubKeyMasternode)) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

        if (AddWinningMasternode(newWinner)) {
            newWinner.Relay();
            nLastBlockHeight = nBlockHeight;
            return true;
        }
    }

    return false;
}

void CMasternodePaymentWinner::Relay()
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid()
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (pmn != NULL) {
        std::string strMessage = vinMasternode.prevout.ToStringShort() +
                boost::lexical_cast<std::string>(nBlockHeight) +
                payee.ToString();

        std::string errorMessage = "";
        if (!CObfuScationSigner::VerifyMessage(pmn->pubKeyMasternode, vchSig, strMessage, errorMessage)) {
            return error("CMasternodePaymentWinner::SignatureValid() - Got bad Masternode address signature %s\n", vinMasternode.prevout.hash.ToString());
        }

        return true;
    }

    return false;
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
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
    node->PushMessage("ssc", MASTERNODE_SYNC_MNW, nInvCount);
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}