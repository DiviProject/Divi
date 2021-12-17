// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip38.h"
#include "base58.h"
#include <boost/filesystem.hpp>
#include <DataDirectory.h>
#include <blockmap.h>
#include <BlockDiskAccessor.h>
#include <chain.h>
#include "init.h"
#include "main.h"
#include "rpcserver.h"
#include "script/script.h"
#include "script/standard.h"
#include "sync.h"
#include "Logging.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "wallet.h"
#include <clientversion.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>

#include <fstream>
#include <secp256k1.h>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <openssl/aes.h>
#include <openssl/sha.h>

#include "json/json_spirit_value.h"

using namespace json_spirit;
using namespace std;
extern CWallet* pwalletMain;
extern CChain chainActive;

void EnsureWalletIsUnlocked();

std::string static EncodeDumpTime(int64_t nTime)
{
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

int64_t static DecodeDumpTime(const std::string& str)
{
    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
    static const std::locale loc(std::locale::classic(),
        new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
    std::istringstream iss(str);
    iss.imbue(loc);
    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
    iss >> ptime;
    if (ptime.is_not_a_date_time())
        return 0;
    return (ptime - epoch).total_seconds();
}

std::string static EncodeDumpString(const std::string& str)
{
    std::stringstream ret;
    BOOST_FOREACH (unsigned char c, str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(&c, &c + 1);
        } else {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string& str)
{
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos + 2 < str.length()) {
            c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
                ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

Value importprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "importprivkey \"diviprivkey\" ( \"label\" rescan )\n"
            "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n"
            "\nArguments:\n"
            "1. \"diviprivkey\"   (string, required) The private key (see dumpprivkey)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "\nNote: This call can take minutes to complete if rescan is true.\n"
            "\nExamples:\n"
            "\nDump a private key\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key with rescan\n" + HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nImport using a label and without rescan\n" + HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" + HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false"));

    EnsureWalletIsUnlocked();

    string strSecret = params[0].get_str();
    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (params.size() > 2)
        fRescan = params[2].get_bool();

    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    CKey key = vchSecret.GetKey();
    if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Private key outside allowed range");

    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->SetAddressBook(vchAddress, strLabel, "receive");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            return Value::null;

        pwalletMain->AddKeyMetadata(pubkey,CKeyMetadata(1));
        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain; 0 would be considered 'no value'
        pwalletMain->UpdateTimeFirstKey(1);

        if (fRescan) {
            ScanForWalletTransactions(*pwalletMain,chainActive.Genesis(),true);
        }
    }

    return Value::null;
}

Value importaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
            "importaddress \"address\" ( \"label\" rescan )\n"
            "\nAdds an address or script (in hex) that can be watched as if it were in your wallet but cannot be used to spend.\n"
            "\nArguments:\n"
            "1. \"address\"          (string, required) The address\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "\nNote: This call can take minutes to complete if rescan is true.\n"
            "\nExamples:\n"
            "\nImport an address with rescan\n" +
            HelpExampleCli("importaddress", "\"myaddress\"") +
            "\nImport using a label without rescan\n" + HelpExampleCli("importaddress", "\"myaddress\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" + HelpExampleRpc("importaddress", "\"myaddress\", \"testing\", false"));

    CScript script;

    CBitcoinAddress address(params[0].get_str());
    if (address.IsValid()) {
        script = GetScriptForDestination(address.Get());
    } else if (IsHex(params[0].get_str())) {
        std::vector<unsigned char> data(ParseHex(params[0].get_str()));
        script = CScript(data.begin(), data.end());
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address or script");
    }

    string strLabel = "";
    if (params.size() > 1)
        strLabel = params[1].get_str();

    // Whether to perform rescan after import
    bool fRescan = true;
    if (params.size() > 2)
        fRescan = params[2].get_bool();

    {
        if(!pwalletMain)
            throw JSONRPCError(RPC_WALLET_ERROR,"Wallet is not enabled in this build");

        if (pwalletMain->IsMine(address.Get()) == isminetype::ISMINE_SPENDABLE)
            throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

        // add to address book or update label
        if (address.IsValid())
            pwalletMain->SetAddressBook(address.Get(), strLabel, "receive");

        // Don't throw error in case an address is already there
        if (pwalletMain->HaveWatchOnly(script))
            return Value::null;

        if (!pwalletMain->AddWatchOnly(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

        if (fRescan) {
            ScanForWalletTransactions(*pwalletMain,chainActive.Genesis(),true);
        }
    }

    return Value::null;
}

Value importwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "importwallet \"filename\"\n"
            "\nImports keys from a wallet dump file (see dumpwallet).\n"
            "\nArguments:\n"
            "1. \"filename\"    (string, required) The wallet file\n"
            "\nExamples:\n"
            "\nDump the wallet\n" +
            HelpExampleCli("dumpwallet", "\"test\"") +
            "\nImport the wallet\n" + HelpExampleCli("importwallet", "\"test\"") +
            "\nImport using the json rpc call\n" + HelpExampleRpc("importwallet", "\"test\""));

    EnsureWalletIsUnlocked();

    ifstream file;
    file.open(params[0].get_str().c_str(), std::ios::in | std::ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    int64_t nTimeBegin = chainActive.Tip()->GetBlockTime();

    bool fGood = true;

    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);

    pwalletMain->ShowProgress(translate("Importing..."), 0); // show progress dialog in GUI
    while (file.good()) {
        pwalletMain->ShowProgress("", std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;

        std::vector<std::string> vstr;
        boost::split(vstr, line, boost::is_any_of(" "));
        if (vstr.size() < 2)
            continue;
        CBitcoinSecret vchSecret;
        if (!vchSecret.SetString(vstr[0]))
            continue;
        CKey key = vchSecret.GetKey();
        CPubKey pubkey = key.GetPubKey();
        assert(key.VerifyPubKey(pubkey));
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid)) {
            LogPrintf("Skipping import of %s (key already present)\n", CBitcoinAddress(keyid));
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        std::string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
            if (boost::algorithm::starts_with(vstr[nStr], "#"))
                break;
            if (vstr[nStr] == "change=1")
                fLabel = false;
            if (vstr[nStr] == "reserve=1")
                fLabel = false;
            if (boost::algorithm::starts_with(vstr[nStr], "label=")) {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        LogPrintf("Importing %s...\n", CBitcoinAddress(keyid));
        if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
            fGood = false;
            continue;
        }
        pwalletMain->AddKeyMetadata(pubkey,CKeyMetadata(nTime));
        if (fLabel)
            pwalletMain->SetAddressBook(keyid, strLabel, "receive");
        nTimeBegin = std::min(nTimeBegin, nTime);
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

    CBlockIndex* pindex = chainActive.Tip();
    while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200)
        pindex = pindex->pprev;

    pwalletMain->UpdateTimeFirstKey(nTimeBegin);

    LogPrintf("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
    ScanForWalletTransactions(*pwalletMain,pindex);

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return Value::null;
}

Value dumpprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpprivkey \"diviaddress\"\n"
            "\nReveals the private key corresponding to 'diviaddress'.\n"
            "Then the importprivkey can be used with this output\n"
            "\nArguments:\n"
            "1. \"diviaddress\"   (string, required) The divi address for the private key\n"
            "\nResult:\n"
            "\"key\"                (string) The private key\n"
            "\nExamples:\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") + HelpExampleCli("importprivkey", "\"mykey\"") + HelpExampleRpc("dumpprivkey", "\"myaddress\""));

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return CBitcoinSecret(vchSecret).ToString();
}

Value dumphdinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
            "dumphdinfo\n"
            "Returns an object containing sensitive private info about this HD wallet.\n"
            "\nResult:\n"
            "{\n"
            "  \"hdseed\": \"seed\",                    (string) The HD seed (bip32, in hex)\n"
            "  \"mnemonic\": \"words\",                 (string) The mnemonic for this HD wallet (bip39, english words) \n"
            "  \"mnemonicpassphrase\": \"passphrase\",  (string) The mnemonic passphrase for this HD wallet (bip39)\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("dumphdinfo", "")
            + HelpExampleRpc("dumphdinfo", "")
        );

    LOCK(pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    CHDChain hdChainCurrent;
    if (!pwalletMain->GetHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_WALLET_ERROR, "This wallet is not a HD wallet.");

    if (!pwalletMain->GetDecryptedHDChain(hdChainCurrent))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD seed");

    SecureString ssMnemonic;
    SecureString ssMnemonicPassphrase;
    hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);

    Object obj;
    obj.push_back(Pair("hdseed", HexStr(hdChainCurrent.GetSeed())));
    obj.push_back(Pair("mnemonic", ssMnemonic.c_str()));
    obj.push_back(Pair("mnemonicpassphrase", ssMnemonicPassphrase.c_str()));

    return obj;
}

Value dumpwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpwallet \"filename\"\n"
            "\nDumps all wallet keys in a human-readable format.\n"
            "\nArguments:\n"
            "1. \"filename\"    (string, required) The filename\n"
            "\nExamples:\n" +
            HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

    EnsureWalletIsUnlocked();

    ofstream file;
    file.open(params[0].get_str().c_str());
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    std::map<CKeyID, int64_t> mapKeyBirth;
    std::set<CKeyID> setKeyPool;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    pwalletMain->GetAllReserveKeys(setKeyPool);

    // sort time/key pairs
    std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
    for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++) {
        vKeyBirth.push_back(std::make_pair(it->second, it->first));
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    // produce output
    file << strprintf("# Wallet dump created by DIVI %s (%s)\n", CLIENT_BUILD, CLIENT_DATE);
    file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()));
    file << strprintf("# * Best block at time of backup was %i (%s),\n", chainActive.Height(), chainActive.Tip()->GetBlockHash().ToString());
    file << strprintf("#   mined on %s\n", EncodeDumpTime(chainActive.Tip()->GetBlockTime()));
    file << "\n";

    // add the base58check encoded extended master if the wallet uses HD
    CHDChain hdChainCurrent;
    if (pwalletMain->GetHDChain(hdChainCurrent))
    {

        if (!pwalletMain->GetDecryptedHDChain(hdChainCurrent))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Cannot decrypt HD chain");

        SecureString ssMnemonic;
        SecureString ssMnemonicPassphrase;
        hdChainCurrent.GetMnemonic(ssMnemonic, ssMnemonicPassphrase);
        file << "# mnemonic: " << ssMnemonic << "\n";
        file << "# mnemonic passphrase: " << ssMnemonicPassphrase << "\n\n";

        SecureVector vchSeed = hdChainCurrent.GetSeed();
        file << "# HD seed: " << HexStr(vchSeed) << "\n\n";

        CExtKey masterKey;
        masterKey.SetMaster(&vchSeed[0], vchSeed.size());

        CBitcoinExtKey b58extkey;
        b58extkey.SetKey(masterKey);

        file << "# extended private masterkey: " << b58extkey.ToString() << "\n";

        CExtPubKey masterPubkey;
        masterPubkey = masterKey.Neuter();

        CBitcoinExtPubKey b58extpubkey;
        b58extpubkey.SetKey(masterPubkey);
        file << "# extended public masterkey: " << b58extpubkey.ToString() << "\n\n";

        for (size_t i = 0; i < hdChainCurrent.CountAccounts(); ++i)
        {
            CHDAccount acc;
            if(hdChainCurrent.GetAccount(i, acc)) {
                file << "# external chain counter: " << acc.nExternalChainCounter << "\n";
                file << "# internal chain counter: " << acc.nInternalChainCounter << "\n\n";
            } else {
                file << "# WARNING: ACCOUNT " << i << " IS MISSING!" << "\n\n";
            }
        }
    }


    for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++) {
        const CKeyID& keyid = it->second;
        std::string strTime = EncodeDumpTime(it->first);
        std::string strAddr = CBitcoinAddress(keyid).ToString();
        CKey key;
        if (pwalletMain->GetKey(keyid, key)) {
            const AddressBook& addressBook = pwalletMain->GetAddressBook();
            if (addressBook.count(keyid)) {
                file << strprintf("%s %s label=%s # addr=%s", CBitcoinSecret(key).ToString(), strTime, EncodeDumpString(addressBook.find(keyid)->second.name), strAddr);
            } else if (setKeyPool.count(keyid)) {
                file << strprintf("%s %s reserve=1 # addr=%s", CBitcoinSecret(key).ToString(), strTime, strAddr);
            } else {
                file << strprintf("%s %s change=1 # addr=%s", CBitcoinSecret(key).ToString(), strTime, strAddr);
            }

            CKeyMetadata metadata = pwalletMain->getKeyMetadata(CBitcoinAddress(keyid));
            file << strprintf(" %s", metadata.hdkeypath);

            file << "\n";
        }
    }
    file << "\n";
    file << "# End of dump\n";
    file.close();
    return Value::null;
}

Value bip38encrypt(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "bip38encrypt \"diviaddress\"\n"
            "\nEncrypts a private key corresponding to 'diviaddress'.\n"
            "\nArguments:\n"
            "1. \"diviaddress\"   (string, required) The divi address for the private key (you must hold the key already)\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with - Valid special chars: !#$%&'()*+,-./:;<=>?`{|}~ \n"
            "\nResult:\n"
            "\"key\"                (string) The encrypted private key\n"
            "\nExamples:\n");

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    string strPassphrase = params[1].get_str();

    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");

#if 0 // TODO: check this
#endif
    uint256 privKey;// = vchSecret.GetPrivKey_256();
    string encryptedOut = BIP38_Encrypt(strAddress, strPassphrase, privKey, vchSecret.IsCompressed());

    Object result;
    result.push_back(Pair("Addess", strAddress));
    result.push_back(Pair("Encrypted Key", encryptedOut));

    return result;
}

static bool CheckIntegrity(const std::string strAddress,const std::string strKey,const std::string strPassphrase)
{
    uint256 privKey;
    bool fCompressed;
    if (!BIP38_Decrypt(strPassphrase, strKey, privKey, fCompressed))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed To Decrypt");

    CKey key;
    key.Set(privKey.begin(), privKey.end(), fCompressed);
    std::string addressParsed = CBitcoinAddress(key.GetPubKey().GetID()).ToString();
    return addressParsed.compare(strAddress)==0;
}

class DummyConfirmationCalculator final: public I_MerkleTxConfirmationNumberCalculator
{
public:
    std::pair<const CBlockIndex*,int> FindConfirmedBlockIndexAndDepth(const CMerkleTx& merkleTx) const override
    {
        return std::make_pair(nullptr,-1);
    }
    int GetNumberOfBlockConfirmations(const CMerkleTx& merkleTx) const override
    {
        return -1;
    }
    int GetBlocksToMaturity(const CMerkleTx& merkleTx) const override
    {
        return 1000;
    }
};

Value bip38paperwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "bip38encrypt \"password\"\n"
            "\nEncrypts a fresh private key with <password>.\n"
            "\nArguments:\n"
            "1. \"diviaddress\"   (string, required) The divi address for the private key (you must hold the key already)\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with - Valid special chars: !#$%&'()*+,-./:;<=>?`{|}~ \n"
            "\nResult:\n"
            "\"key\"                (string) The encrypted private key\n"
            "\nExamples:\n");

    EnsureWalletIsUnlocked();

    std::string strPassphrase;
    std::string strAddress;
    uint256 privKey;
    bool compressedKey = false;
    const std::string walletDummyFilename = "paper_wallet.dat";
    {
        CChain dummyChain;
        BlockMap dummyBlockMap;
        DummyConfirmationCalculator dummyConfs;
        std::unique_ptr<CWallet> temporaryWallet(new CWallet(walletDummyFilename,dummyChain,dummyBlockMap,dummyConfs));
        temporaryWallet->SetDefaultKeyTopUp(1);
        temporaryWallet->LoadWallet();
        temporaryWallet->GenerateNewHDChain();
        temporaryWallet->InitializeDefaultKey();
        const CPubKey& tempPubKey = temporaryWallet->GetDefaultKey();
        if(!tempPubKey.IsValid())
        {
            temporaryWallet.reset();
            boost::filesystem::remove(GetDataDir()/walletDummyFilename);
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid key");
        }
        const CKeyID keyID = tempPubKey.GetID();

        strPassphrase = params[0].get_str();
        strAddress = CBitcoinAddress(keyID).ToString();

        CKey key;
        if(!temporaryWallet->GetKey(keyID,key))
        {
            temporaryWallet.reset();
            boost::filesystem::remove(GetDataDir()/walletDummyFilename);
            throw JSONRPCError(RPC_WALLET_ERROR, "Unknown error when generating fresh throwaway wallet");
        }
        privKey = uint256(ToByteVector(key));
        compressedKey = key.IsCompressed();
    }
    boost::filesystem::remove(GetDataDir()/walletDummyFilename);
    const std::string encryptedOut = BIP38_Encrypt(strAddress, strPassphrase, privKey, compressedKey);

    Object result;
    result.push_back(Pair("Address", strAddress));
    result.push_back(Pair("Encrypted Key", encryptedOut));
    assert(CheckIntegrity(strAddress,encryptedOut,strPassphrase));
    return result;
}

Value bip38decrypt(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "bip38decrypt \"diviaddress\"\n"
            "\nDecrypts and then imports password protected private key.\n"
            "\nArguments:\n"
            "1. \"encryptedkey\"   (string, required) The encrypted private key\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with\n"

            "\nResult:\n"
            "\"key\"                (string) The decrypted private key\n"
            "\nExamples:\n");

    EnsureWalletIsUnlocked();

    /** Collect private key and passphrase **/
    string strKey = params[0].get_str();
    string strPassphrase = params[1].get_str();

    uint256 privKey;
    bool fCompressed;
    if (!BIP38_Decrypt(strPassphrase, strKey, privKey, fCompressed))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed To Decrypt");

    Object result;
    CKey key;
    key.Set(privKey.begin(), privKey.end(), fCompressed);

    if (!key.IsValid())
        throw JSONRPCError(RPC_WALLET_ERROR, "Private Key Not Valid");

    CPubKey pubkey = key.GetPubKey();
    pubkey.IsCompressed();
    assert(key.VerifyPubKey(pubkey));
    result.push_back(Pair("Address", CBitcoinAddress(pubkey.GetID()).ToString()));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->SetAddressBook(vchAddress, "", "receive");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            throw JSONRPCError(RPC_WALLET_ERROR, "Key already held by wallet");

        pwalletMain->AddKeyMetadata(pubkey,CKeyMetadata(1));
        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain; 0 would be considered 'no value'
        pwalletMain->UpdateTimeFirstKey(1);
        ScanForWalletTransactions(*pwalletMain,chainActive.Genesis(), true);
    }

    return result;
}
