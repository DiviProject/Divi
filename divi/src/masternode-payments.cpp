// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"

#include "activemasternode.h"
#include "addrman.h"
#include "BlockDiskAccessor.h"
#include <chain.h>
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
#include <BlockIncentivesPopulator.h>
#include <script/standard.h>
#include <base58address.h>
#include <chainparams.h>
#include <LotteryCoinstakes.h>

extern std::string strMasterNodePrivKey;
extern bool fLiteMode;
extern bool fMasterNode;


int ActiveProtocol();
void Misbehaving(NodeId pnode, int howmuch);
extern CCriticalSection cs_main;
extern CChain chainActive;

/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

namespace
{

constexpr int MNPAYMENTS_SIGNATURES_REQUIRED = 6;
constexpr int MNPAYMENTS_SIGNATURES_TOTAL = 10;
}

bool HasValidMasternodePayee(const CTransaction &txNew, const CBlockIndex* pindex)
{
    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrintf("%s : Client not synced, skipping block payee checks\n", __func__);
        return true;
    }

    /* For the first 100 blocks after genesis, there is no scoring hash (as
       the block used for it would be before genesis).  In this case, just
       ignore any payment checks.  On mainnet, those blocks are long enshrined
       into blockchain history anyway.  On regtest, this allows proper
       functioning.  */
    if (pindex->nHeight <= 100) {
        LogPrint("masternode", "%s : not checking payments for height %d\n",
                 __func__, pindex->nHeight);
        return true;
    }

    //check for masternode payee
    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindex, 0)) {
        LogPrint("masternode", "%s : failed to get scoring hash for height %d\n",
                 __func__, pindex->nHeight);
        return false;
    }
    if (masternodePayments.IsTransactionValid(txNew, seedHash))
        return true;
    LogPrintf("%s : Invalid mn payment detected %s\n", __func__, txNew.ToString().c_str());

    if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
        return false;
    LogPrintf("%s : Masternode payment enforcement is disabled, accepting block\n", __func__);

    return true;
}

CMasternodePayee::CMasternodePayee()
{
    scriptPubKey = CScript();
    nVotes = 0;
}

CMasternodePayee::CMasternodePayee(const CScript& payee, int nVotesIn)
{
    scriptPubKey = payee;
    nVotes = nVotesIn;
}

CMasternodeBlockPayees::CMasternodeBlockPayees()
{
    nBlockHeight = 0;
    vecPayments.clear();
}
CMasternodeBlockPayees::CMasternodeBlockPayees(int nBlockHeightIn)
{
    nBlockHeight = nBlockHeightIn;
    vecPayments.clear();
}

void CMasternodeBlockPayees::CountVote(const COutPoint& voter, const CScript& payeeIn)
{
    LOCK(cs_vecPayments);

    const auto ins = voters.insert(voter);
    assert(ins.second);

    for (auto& payee : vecPayments) {
        if (payee.scriptPubKey == payeeIn) {
            ++payee.nVotes;
            return;
        }
    }

    CMasternodePayee c(payeeIn, 1);
    vecPayments.push_back(c);
}

bool CMasternodeBlockPayees::CanVote(const COutPoint& voter) const
{
    LOCK(cs_vecPayments);
    return voters.count(voter) == 0;
}

bool CMasternodeBlockPayees::GetPayee(CScript& payee) const
{
    LOCK(cs_vecPayments);

    int nVotes = -1;
    for (const auto& p : vecPayments) {
        if (p.nVotes > nVotes) {
            payee = p.scriptPubKey;
            nVotes = p.nVotes;
        }
    }

    return (nVotes > -1);
}

bool CMasternodeBlockPayees::HasPayeeWithVotes(const CScript& payee, int nVotesReq) const
{
    LOCK(cs_vecPayments);

    for (const auto& p : vecPayments) {
        if (p.nVotes >= nVotesReq && p.scriptPubKey == payee) return true;
    }

    return false;
}


bool CMasternodePaymentWinner::ComputeScoreHash()
{
    if (GetBlockHashForScoring(seedHash, nBlockHeight))
        return true;

    LogPrint("masternode", "Failed to get scoring hash for winner of height %d\n", nBlockHeight);
    seedHash.SetNull();
    return false;
}

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << payee;
    ss << nBlockHeight;
    ss << vinMasternode.prevout;

    return ss.GetHash();
}

void CMasternodePaymentWinner::AddPayee(const CScript& payeeIn)
{
    payee = payeeIn;
}

const uint256&
CMasternodePaymentWinner::GetScoreHash() const
{
    assert(!seedHash.IsNull());
    return seedHash;
}

std::string CMasternodePaymentWinner::ToString() const
{
    std::string ret = "";
    ret += vinMasternode.ToString();
    ret += ", " + boost::lexical_cast<std::string>(nBlockHeight);
    ret += ", " + payee.ToString();
    ret += ", " + boost::lexical_cast<std::string>((int)vchSig.size());
    return ret;
}

CMasternodePayments::CMasternodePayments()
{
    nSyncedFromPeer = 0;
    nLastBlockHeight = 0;
}

void CMasternodePayments::Clear()
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);
    mapMasternodeBlocks.clear();
    mapMasternodePayeeVotes.clear();
}

bool CMasternodePayments::CanVote(const COutPoint& outMasternode, const uint256& seedHash)
{
    LOCK(cs_mapMasternodePayeeVotes);

    const auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees == nullptr)
        return true;

    return payees->CanVote(outMasternode);
}


void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, const CBlockRewards &rewards, bool fProofOfStake) const
{
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    bool hasPayment = true;
    CScript payee;

    //spork
    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindexPrev, 1)) {
        LogPrint("masternode", "FillBlockPayee - failed to get score hash\n");
        return;
    }
    if (!GetBlockPayee(seedHash, payee)) {
        // No masternode detected, fall back to our own queue.
        const CMasternode* winningNode = mnodeman.GetNextMasternodeInQueueForPayment(pindexPrev, 1, true);
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

int CMasternodePayments::GetMinMasternodePaymentsProto() const
{
    //    if (IsSporkActive(SPORK_10_MASTERNODE_PAY_UPDATED_NODES))
    return ActiveProtocol();                          // Allow only updated peers
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv)
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
        Sync(pfrom, nCountNeeded);
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

        if (GetPaymentWinnerForHash(winner.GetHash()) != nullptr) {
            LogPrint("mnpayments", "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.GetHeight() < nFirstBlock || winner.GetHeight() > nHeight + 20) {
            LogPrint("mnpayments", "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.GetHeight(), nHeight);
            return;
        }

        if (!winner.ComputeScoreHash()) {
            LogPrint("mnpayments", "mnw - could not compute score hash for height %d\n", winner.GetHeight());
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint("masternode","mnw - invalid message - %s\n", strError);
            return;
        }

        if (!CanVote(winner.vinMasternode.prevout, winner.GetScoreHash())) {
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

        if (AddWinningMasternode(winner)) {
            winner.Relay();
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePaymentWinner::Sign(const CKey& keyMasternode, const CPubKey& pubKeyMasternode)
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

bool CMasternodePayments::GetBlockPayee(const uint256& seedHash, CScript& payee) const
{
    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->GetPayee(payee);

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(const CMasternode& mn, int nNotBlockHeight) const
{
    LOCK(cs_mapMasternodeBlocks);

    CBlockIndex* tip = nullptr;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked) return false;
        tip = chainActive.Tip();
    }
    if (tip == nullptr)
        return false;

    const CScript mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    for (int64_t h = 0; h <= 8; ++h) {
        if (tip->nHeight + h == nNotBlockHeight) continue;
        uint256 seedHash;
        if (!GetBlockHashForScoring(seedHash, tip, h)) continue;
        auto* payees = GetPayeesForScoreHash(seedHash);
        CScript payee;
        if (payees != nullptr && payees->GetPayee(payee) && payee == mnpayee)
            return true;
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(const CMasternodePaymentWinner& winnerIn)
{
    CMasternodeBlockPayees* payees;
    {
        LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

        if (GetPaymentWinnerForHash(winnerIn.GetHash()) != nullptr)
            return false;

        auto ins = mapMasternodePayeeVotes.emplace(winnerIn.GetHash(), winnerIn);
        assert(ins.second);

        payees = GetPayeesForScoreHash(winnerIn.GetScoreHash());
        if (payees == nullptr) {
            CMasternodeBlockPayees blockPayees(winnerIn.GetHeight());
            auto mit = mapMasternodeBlocks.emplace(winnerIn.GetScoreHash(), std::move(blockPayees)).first;
            payees = &mit->second;
        }
    }

    payees->CountVote(winnerIn.vinMasternode.prevout, winnerIn.payee);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew) const
{
    LOCK(cs_vecPayments);

    int nMaxSignatures = 0;

    std::string strPayeesPossible = "";

    SuperblockSubsidyContainer subsidiesContainer(Params());
    auto rewards = subsidiesContainer.blockSubsidiesProvider().GetBlockSubsidity(nBlockHeight);

    CAmount requiredMasternodePayment = rewards.nMasternodeReward;

    //require at least 6 signatures
    for(const auto& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    for (const auto& payee : vecPayments) {
        bool found = false;
        for (const auto& out : txNew.vout) {
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

std::string CMasternodeBlockPayees::GetRequiredPaymentsString() const
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (const auto& payee : vecPayments) {
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

std::string CMasternodePayments::GetRequiredPaymentsString(const uint256& seedHash) const
{
    LOCK(cs_mapMasternodeBlocks);

    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->GetRequiredPaymentsString();

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, const uint256& seedHash) const
{
    LOCK(cs_mapMasternodeBlocks);

    auto* payees = GetPayeesForScoreHash(seedHash);
    if (payees != nullptr)
        return payees->IsTransactionValid(txNew);

    return true;
}

void CMasternodePayments::CheckAndRemove()
{
    LOCK2(cs_mapMasternodeBlocks, cs_mapMasternodePayeeVotes);

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

        if (nHeight - winner.GetHeight() > nLimit) {
            LogPrint("mnpayments", "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.GetHeight());
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.GetScoreHash());
        } else {
            ++it;
        }
    }
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError) const
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

    /* Before accepting a payment as valid, explicitly check that the
       masternode is active.  GetMasternodeRank includes this check, but
       has a cache on results so double-checking doesn't hurt.  */
    pmn->Check();
    if (!pmn->IsEnabled()) {
        strError = strprintf("Masternode %s is not active", vinMasternode.prevout.hash.ToString());
        LogPrint("masternode", "CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    assert(!seedHash.IsNull());
    const unsigned n = mnodeman.GetMasternodeRank(vinMasternode, seedHash, ActiveProtocol(), 2 * MNPAYMENTS_SIGNATURES_TOTAL);

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%u)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint("masternode","CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    if(!masternodeSync.IsSynced()){ return true;}

    /* Make sure that the payee is in our own payment queue near the top.  */
    const std::vector<CMasternode*> mnQueue = mnodeman.GetMasternodePaymentQueue(seedHash, nBlockHeight, true);
    for (int i = 0; i < std::min<int>(2 * MNPAYMENTS_SIGNATURES_TOTAL, mnQueue.size()); ++i) {
        const auto& mn = *mnQueue[i];
        const CScript mnPayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());
        if (mnPayee == payee)
            return true;
    }
    return false;
}

bool CMasternodePayments::ProcessBlock(const CBlockIndex* pindex, const int offset)
{
    if (!fMasterNode) return false;
    const int64_t nBlockHeight = pindex->nHeight + offset;

    //reference node - hybrid mode

    uint256 seedHash;
    if (!GetBlockHashForScoring(seedHash, pindex, offset)) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - failed to compute seed hash\n");
        return false;
    }

    const unsigned n = mnodeman.GetMasternodeRank(activeMasternode.vin, seedHash, ActiveProtocol(), MNPAYMENTS_SIGNATURES_TOTAL);

    if (n == static_cast<unsigned>(-1)) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
        return false;
    }

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
        return false;
    }

    if (nBlockHeight <= nLastBlockHeight) return false;

    CMasternodePaymentWinner newWinner(activeMasternode.vin, nBlockHeight, seedHash);

    LogPrint("masternode","CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin.prevout.hash.ToString());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(pindex, offset, true);

    if (pmn != NULL) {
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

        const CTxDestination dest(pmn->pubKeyCollateralAddress.GetID());
        newWinner.AddPayee(GetScriptForDestination(dest));

        const CBitcoinAddress address(dest);
        LogPrint("masternode","CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", address.ToString().c_str(), newWinner.GetHeight());
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

void CMasternodePaymentWinner::Relay() const
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    RelayInv(inv);
}

bool CMasternodePaymentWinner::SignatureValid() const
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
        if (winner.GetHeight() >= nHeight - nCountNeeded && winner.GetHeight() <= nHeight + 20) {
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
