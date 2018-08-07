// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "obfuscation-relay.h"

CObfuScationRelay::CObfuScationRelay()
{
    vinMasternode = CTxIn();
    nBlockHeight = 0;
    nRelayType = 0;
    in = CTxIn();
    out = CTxOut();
}

CObfuScationRelay::CObfuScationRelay(CTxIn& vinMasternodeIn, vector<unsigned char>& vchSigIn, int nBlockHeightIn, int nRelayTypeIn, CTxIn& in2, CTxOut& out2)
{
    vinMasternode = vinMasternodeIn;
    vchSig = vchSigIn;
    nBlockHeight = nBlockHeightIn;
    nRelayType = nRelayTypeIn;
    in = in2;
    out = out2;
}

std::string CObfuScationRelay::ToString()
{
    std::ostringstream info;

    info << "vin: " << vinMasternode.ToString() << " nBlockHeight: " << (int)nBlockHeight << " nRelayType: " << (int)nRelayType << " in " << in.ToString() << " out " << out.ToString();

    return info.str();
}

bool CObfuScationRelay::Sign(std::string strSharedKey)
{
    //std::string strMessage = in.ToString() + out.ToString();

    //CKey key2;
    //CPubKey pubkey2;
    //std::string errorMessage = "";

    //if (!obfuScationSigner.SetKey(strSharedKey, errorMessage, key2, pubkey2)) {
    //    LogPrintf("CObfuScationRelay():Sign - ERROR: Invalid shared key: '%s'\n", errorMessage.c_str());
    //    return false;
    //}

    //if (!obfuScationSigner.SignMessage(strMessage, errorMessage, vchSig2, key2)) {
    //    LogPrintf("CObfuScationRelay():Sign - Sign message failed\n");
    //    return false;
    //}

    //if (!obfuScationSigner.VerifyMessage(pubkey2, vchSig2, strMessage, errorMessage)) {
    //    LogPrintf("CObfuScationRelay():Sign - Verify message failed\n");
    //    return false;
    //}

    return true;
}

bool CObfuScationRelay::VerifyMessage(std::string strSharedKey)
{
    //std::string strMessage = in.ToString() + out.ToString();

    //CKey key2;
    //CPubKey pubkey2;
    //std::string errorMessage = "";

    //if (!obfuScationSigner.SetKey(strSharedKey, errorMessage, key2, pubkey2)) {
    //    LogPrintf("CObfuScationRelay()::VerifyMessage - ERROR: Invalid shared key: '%s'\n", errorMessage.c_str());
    //    return false;
    //}

    //if (!obfuScationSigner.VerifyMessage(pubkey2, vchSig2, strMessage, errorMessage)) {
    //    LogPrintf("CObfuScationRelay()::VerifyMessage - Verify message failed\n");
    //    return false;
    //}

    return true;
}

void CObfuScationRelay::Relay()
{
    //int nCount = std::min(mnodeman.CountEnabled(ActiveProtocol()), 20);
    //int nRank1 = (rand() % nCount) + 1;
    //int nRank2 = (rand() % nCount) + 1;

    ////keep picking another second number till we get one that doesn't match
    //while (nRank1 == nRank2)
    //    nRank2 = (rand() % nCount) + 1;

    ////printf("rank 1 - rank2 %d %d \n", nRank1, nRank2);

    ////relay this message through 2 separate nodes for redundancy
    //RelayThroughNode(nRank1);
    //RelayThroughNode(nRank2);
}

void CObfuScationRelay::RelayThroughNode(int nRank)
{
    //CMasternode* pmn = mnodeman.GetMasternodeByRank(nRank, nBlockHeight, ActiveProtocol());

    //if (pmn != NULL) {
    //    //printf("RelayThroughNode %s\n", pmn->addr.ToString().c_str());
    //    CNode* pnode = ConnectNode((CAddress)pmn->addr, NULL, false);
    //    if (pnode) {
    //        //printf("Connected\n");
    //        pnode->PushMessage("dsr", (*this));
    //        pnode->Release();
    //        return;
    //    }
    //} else {
    //    //printf("RelayThroughNode NULL\n");
    //}
}
