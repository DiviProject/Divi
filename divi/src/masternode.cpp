// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Copyright (c) 2017-2018 The Divi Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "addrman.h"
#include "masternodeman.h"
#include "masternode-payments.h"
#include "sync.h"
#include "util.h"
#include <boost/filesystem.hpp>

using namespace std;

template<typename T>
class MnObject {
public:
	string Read(T& mnObject, string strFile);
	string Write(T& mnObject, string strFile);
};

template<typename T>
string MnObject<T>::Read(T& mnObject, string strFile)	// allows reading of mnodeman and mnpayments from file
{
	int64_t nStart = GetTimeMillis();
	// open input file, and associate with CAutoFile
	boost::filesystem::path pathDB = GetDataDir() / strFile;
	FILE* file = fopen(pathDB.string().c_str(), "rb");
	CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
	if (filein.IsNull()) return "Read : Failed to open file " + pathDB.string();

	// use file size to size memory buffer
	int fileSize = boost::filesystem::file_size(pathDB);
	int dataSize = fileSize - sizeof(uint256);
	vector<unsigned char> vchData;
	vchData.resize(dataSize);
	uint256 hashIn;

	// read data and checksum from file
	try {
		filein.read((char*)&vchData[0], dataSize);
		filein >> hashIn;
	}
	catch (exception e) { return "Read : Deserialize or I/O error - " + string(e.what()); }
	filein.fclose();

	CDataStream dStream(vchData, SER_DISK, CLIENT_VERSION);
	if (hashIn != Hash(dStream.begin(), dStream.end())) return "Read : Checksum mismatch, data corrupted";

	unsigned char pchMsgTmp[4];
	std::string strMagicMessageTmp;
	try {
		dStream >> mnObject >> FLATDATA(pchMsgTmp) >> strMagicMessageTmp;
		if (strFile != strMagicMessageTmp) return "Read : Invalid masternode cache magic message";
		if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) return "Read : Invalid network magic number";
	}
	catch (std::exception& e) { return "Read : Deserialize or I/O error - " + string(e.what()); }

	return "Loaded info from " + strFile + " in " + to_string(GetTimeMillis() - nStart) + " milliseconds";
}

template<typename T>
string MnObject<T>::Write(T& mnObject, string strFile)	// allows dumping of mnodeman and mnpayments to file
{
	int64_t nStart = GetTimeMillis();
	boost::filesystem::path pathDB = GetDataDir() / strFile;
	// serialize, checksum data up to that point, then append checksum
	CDataStream dStream(SER_DISK, CLIENT_VERSION);
	dStream << strFile << FLATDATA(Params().MessageStart()) << mnObject;
	uint256 hash = Hash(dStream.begin(), dStream.end());
	dStream << hash;

	// open output file, and associate with CAutoFile
	FILE* file = fopen(pathDB.string().c_str(), "wb");
	CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
	if (fileout.IsNull()) return "Write : Failed to open file" + pathDB.string();

	// Write and commit header, data
	try { fileout << dStream; }
	catch (std::exception& e) { return "Write : Serialize or I/O error - " + string(e.what()); }
	fileout.fclose();

	return "Wrote info to " + strFile + " in " + to_string(GetTimeMillis() - nStart) + " milliseconds";
}

bool CMnFunding::CheckVin(string address) {
	// TODO: check signature
	CTransaction vTx;
	uint256 hashBlock;
	if (!GetTransaction(vin.prevout.hash, vTx, hashBlock, false)) fprintf(stderr, "Can't get TX");
	
	CValidationState state;
	CMutableTransaction tx = CMutableTransaction();

	CTxOut vout = CTxOut(amount - 1000000, GetScriptForDestination(CBitcoinAddress(address).Get()));
	tx.vin.push_back(vin);
	tx.vout.push_back(vout);
	{
		TRY_LOCK(cs_main, lockMain);
		if (!lockMain) return false;
		return (AcceptableInputs(mempool, state, CTransaction(tx), false, NULL));
	}
}

bool CMasternode::IsEnabled()
{
	if (GetAdjustedTime() - lastTimeChecked < MASTERNODE_CHECK_SECONDS) return true;
	lastTimeChecked = GetAdjustedTime();
 	if (lastPing.sigTime + MASTERNODE_EXPIRATION_SECONDS < lastTimeChecked) { return false; }
	return (VerifyFunding() == "");
}

string CMasternode::ReadDBs() {
	CMasternodeMan tempMnodeman;

	string str1 = MnObject<CMasternodeMan>().Read(tempMnodeman, "mncache.dat");
	string str2 = MnObject<CMasternodePayments>().Read(mnPayments, "mnpayments.dat");
	return str1 + "/n" + str2;
}

string CMasternode::SignMsg(string address, string msg, vector<unsigned char>& vchSig)
{
	LogPrintStr("\n\n\n SIGNING " + address + "\n\n\n");
	CBitcoinAddress address2(address);
	// if (address2.IsValid()) fprintf(stderr, "good address"); else fprintf(stderr, "bad address");
	CKeyID keyID;
	address2.GetKeyID(keyID);
	CKey key;
	pwalletMain->GetKey(keyID, key);

	sigTime = GetAdjustedTime();
	CHashWriter ss(SER_GETHASH, 0);
	ss << strMessageMagic << msg << sigTime;
	key.SignCompact(ss.GetHash(), vchSig);
	return(VerifyMsg(address, msg, vchSig));
}

string CMasternode::StartUp()
{
	string errorMsg;
	// if (!fMasterNode) return "Not a masternode";
	if (pwalletMain->IsLocked()) return "Cannot start - Wallet is locked.";
	//if (strMasterNodeAddr.empty()) return "Cannot start -  No external address";
	//CNode* pnode = ConnectNode((CAddress)CService(strMasterNodeAddr), NULL, false);
	//if (!pnode) return "Cannot start - Could not connect to " + service.ToString()
	//pnode->Release();
	protocolVersion = 1;
	sigTime = GetAdjustedTime();
	if ((errorMsg = VerifyFunding()) != "") return "Cannot start - Funding verification failed!  " + errorMsg;
	LogPrintStr("\nbefore = " + EncodeBase64(&vchSig[0], vchSig.size()));
	if ((errorMsg = SignMsg(ToString(), vchSig)) != "") return "Bad mnb signature " + errorMsg;
	LogPrintStr("\nafter = " + EncodeBase64(&vchSig[0], vchSig.size()));
	lastPing.address = address;
	lastPing.sigTime = GetAdjustedTime();
	// if ((errorMsg = SignMsg(lastPing.ToString(), lastPing.vchSig)) != "") return "Bad ping signature " + errorMsg;

	addrman.Add(CAddress(service), CNetAddr("127.0.0.1"), 2 * 60 * 60);
	mnodeman.Add(mnodeman.my);
	return "";
}

string CMasternode::VerifyMsg(string strAddress, string msg, vector<unsigned char>& vchSig)
{
	LogPrintf("\n\n\n VerifyMsg START\n");
	LogPrintStr(strAddress + "\n");
	LogPrintStr(msg + "\n");
	for (vector<unsigned char>::iterator it = vchSig.begin(); it != vchSig.end(); it++) LogPrintStr(to_string(*it));
	LogPrintStr("\n");

	CBitcoinAddress addr(strAddress);
	if (!addr.IsValid()) return "Invalid address";
	CKeyID keyID;
	if (!addr.GetKeyID(keyID)) return "Address does not refer to key";

	CHashWriter ss(SER_GETHASH, 0);
	ss << strMessageMagic << msg;

	CPubKey pubkey;
	if (!pubkey.RecoverCompact(ss.GetHash(), vchSig)) return "Error recovering public key!";
	LogPrintStr(pubkey.GetID().ToString() + " - " + keyID.ToString() + "\n");
	if (pubkey.GetID() != keyID) LogPrintf("BAD KEY!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	if (pubkey.GetID() != keyID) return "Keys don't match!"; else return "";
}

string CMasternode::VerifyFunding() {
	CAmount totalFunding = 0;
	for (vector<CMnFunding>::iterator it = funding.begin(); it != funding.end(); it++) {
		int ageOfFunds = GetInputAge((*it).vin);
		if (ageOfFunds < MASTERNODE_MIN_CONFIRMATIONS) return "Funding needs more confirmations";
		// if (sigTime < GetAdjustedTime() - ageOfFunds) return "Misbehaving masternode";
		if (!(*it).CheckVin(address)) return "Funds have been spent";
		totalFunding += (*it).amount;
	}
	if (totalFunding < tier.collateral) return "Insufficient funds"; else return "";
}

string CMasternode::WriteDBs() {
	string str1 = MnObject<CMasternodeMan>().Write(mnodeman, "mncache.dat");
	string str2 = MnObject<CMasternodePayments>().Write(mnPayments, "mnpayments.dat");
	return str1 + "/n" + str2;
}

