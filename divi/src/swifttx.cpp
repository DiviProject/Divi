// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "swifttx.h"
#include "base58.h"
#include "key.h"
#include "net.h"
#include "obfuscation.h"
#include "protocol.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost;

std::map<uint256, CTransaction> mapTxLockReq;
std::map<uint256, CTransaction> mapTxLockReqRejected;
std::map<uint256, CConsensusVote> mapTxLockVote;
std::map<uint256, CTransactionLock> mapTxLocks;
std::map<COutPoint, uint256> mapLockedInputs;
std::map<uint256, int64_t> mapUnknownVotes; //track votes with no tx for DOS

uint256 CConsensusVote::GetHash() const
{
    return vinMasternode.prevout.hash + vinMasternode.prevout.n + txHash;
}


bool CConsensusVote::SignatureValid()
{
    //std::string errorMessage;
    //std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    ////LogPrintf("verify strMessage %s \n", strMessage);

    //CMasternode* pmn = mnodeman.Find(vinMasternode);

    //if (pmn == NULL) {
    //    LogPrintf("SwiftX::CConsensusVote::SignatureValid() - Unknown Masternode\n");
    //    return false;
    //}

    //if (!obfuScationSigner.VerifyMessage(pmn->pubKeyMasternode, vchMasterNodeSignature, strMessage, errorMessage)) {
    //    LogPrintf("SwiftX::CConsensusVote::SignatureValid() - Verify message failed\n");
    //    return false;
    //}

    return true;
}

bool CConsensusVote::Sign()
{
    //std::string errorMessage;

    //CKey key2;
    //CPubKey pubkey2;
    //std::string strMessage = txHash.ToString().c_str() + boost::lexical_cast<std::string>(nBlockHeight);
    ////LogPrintf("signing strMessage %s \n", strMessage);
    ////LogPrintf("signing privkey %s \n", strMasterNodePrivKey);

    //if (!obfuScationSigner.SetKey(strMasterNodePrivKey, errorMessage, key2, pubkey2)) {
    //    LogPrintf("CConsensusVote::Sign() - ERROR: Invalid masternodeprivkey: '%s'\n", errorMessage);
    //    return false;
    //}

    //if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchMasterNodeSignature, key2)) {
    //    LogPrintf("CConsensusVote::Sign() - Sign message failed");
    //    return false;
    //}

    //if (!obfuScationSigner.VerifyMessage(pubkey2, vchMasterNodeSignature, strMessage, errorMessage)) {
    //    LogPrintf("CConsensusVote::Sign() - Verify message failed");
    //    return false;
    //}

    return true;
}


bool CTransactionLock::SignaturesValid()
{
    //BOOST_FOREACH (CConsensusVote vote, vecConsensusVotes) {
    //    int n = mnodeman.GetMasternodeRank(vote.vinMasternode, vote.nBlockHeight, MIN_SWIFTTX_PROTO_VERSION);

    //    if (n == -1) {
    //        LogPrintf("CTransactionLock::SignaturesValid() - Unknown Masternode\n");
    //        return false;
    //    }

    //    if (n > SWIFTTX_SIGNATURES_TOTAL) {
    //        LogPrintf("CTransactionLock::SignaturesValid() - Masternode not in the top %s\n", SWIFTTX_SIGNATURES_TOTAL);
    //        return false;
    //    }

    //    if (!vote.SignatureValid()) {
    //        LogPrintf("CTransactionLock::SignaturesValid() - Signature not valid\n");
    //        return false;
    //    }
    //}

    return true;
}

void CTransactionLock::AddSignature(CConsensusVote& cv)
{
    vecConsensusVotes.push_back(cv);
}

int CTransactionLock::CountSignatures()
{
    /*
        Only count signatures where the BlockHeight matches the transaction's blockheight.
        The votes have no proof it's the correct blockheight
    */

    if (nBlockHeight == 0) return -1;

    int n = 0;
    BOOST_FOREACH (CConsensusVote v, vecConsensusVotes) {
        if (v.nBlockHeight == nBlockHeight) {
            n++;
        }
    }
    return n;
}
