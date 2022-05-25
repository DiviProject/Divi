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
#include <rpcprotocol.h>
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
        pwalletMain->SetAddressLabel(vchAddress, strLabel);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            return Value::null;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain; 0 would be considered 'no value'

        if (fRescan) {
            BlockDiskDataReader reader;
            pwalletMain->verifySyncToActiveChain(reader,true);
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

        if (pwalletMain->isMine(address.Get()) == isminetype::ISMINE_SPENDABLE)
            throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

        // add to address book or update label
        if (address.IsValid())
            pwalletMain->SetAddressLabel(address.Get(), strLabel);

        // Don't throw error in case an address is already there
        if (pwalletMain->HaveWatchOnly(script))
            return Value::null;

        if (!pwalletMain->AddWatchOnly(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

        if (fRescan) {
            BlockDiskDataReader reader;
            pwalletMain->verifySyncToActiveChain(reader,true);
        }
    }

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
    bool compressedKey = true;
    {
        CKey freshKey;
        freshKey.MakeNewKey(compressedKey);
        CPubKey tempPubKey = freshKey.GetPubKey();
        if(!tempPubKey.IsValid())
        {
            throw JSONRPCError(RPC_WALLET_ERROR, "Invalid key");
        }
        const CKeyID keyID = tempPubKey.GetID();
        strPassphrase = params[0].get_str();
        strAddress = CBitcoinAddress(keyID).ToString();

        privKey = uint256(ToByteVector(freshKey));
        if(compressedKey != freshKey.IsCompressed())
        {
            privKey = uint256(0);
        }
    }
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
        pwalletMain->SetAddressLabel(vchAddress, "");

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            throw JSONRPCError(RPC_WALLET_ERROR, "Key already held by wallet");

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain; 0 would be considered 'no value'
        BlockDiskDataReader reader;
        pwalletMain->verifySyncToActiveChain(reader,true);
    }

    return result;
}
