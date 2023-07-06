// Copyright (c) 2013-2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"
#include "rpcclient.h"

#include "base58.h"
#include <wallet.h>
#include <Account.h>
#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>
#include "test_only.h"

#include <Settings.h>
#include <test/FakeWallet.h>
using namespace std;
using namespace json_spirit;

extern Array createArgs(int nRequired, const char* address1 = NULL, const char* address2 = NULL);
extern Value CallRPC(string args,CWallet* pwallet);

extern CCriticalSection cs_main;
extern Settings& settings;

class RpcWalletTestFramework
{
private:
    FakeBlockIndexWithHashes fakeChain_;
    FakeWallet fakeWallet_;
public:
    CWallet* pwallet;
    RpcWalletTestFramework(): fakeChain_(1,16000000,4), fakeWallet_(fakeChain_), pwallet(&fakeWallet_.getWallet()) {}
};


BOOST_FIXTURE_TEST_SUITE(rpc_wallet_tests, RpcWalletTestFramework)

BOOST_AUTO_TEST_CASE(rpc_addmultisig)
{
    LOCK(pwallet->getWalletCriticalSection());

    rpcfn_type addmultisig = CRPCTable::getRPCTable()["addmultisigaddress"]->actor;

    // old, 65-byte-long:
    const char address1Hex[] = "041431A18C7039660CD9E3612A2A47DC53B69CB38EA4AD743B7DF8245FD0438F8E7270415F1085B9DC4D7DA367C69F1245E27EE5552A481D6854184C80F0BB8456";
    // new, compressed:
    const char address2Hex[] = "029BBEFF390CE736BD396AF43B52A1C14ED52C086B1E5585C15931F68725772BAC";

    Value v;
    CBitcoinAddress address;
    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex), false,pwallet));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex, address2Hex), false,pwallet));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(2, address1Hex, address2Hex), false,pwallet));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_THROW(addmultisig(createArgs(0), false,pwallet), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1), false,pwallet), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(2, address1Hex), false,pwallet), runtime_error);

    BOOST_CHECK_THROW(addmultisig(createArgs(1, ""), false,pwallet), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1, "NotAValidPubkey"), false,pwallet), runtime_error);

    string short1(address1Hex, address1Hex + sizeof(address1Hex) - 2); // last byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short1.c_str()), false,pwallet), runtime_error);

    string short2(address1Hex + 1, address1Hex + sizeof(address1Hex)); // first byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short2.c_str()), false,pwallet), runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_wallet)
{
    // Test RPC calls for various wallet statistics
    Value r;
    LOCK2(cs_main, pwallet->getWalletCriticalSection());

    CPubKey demoPubkey;
    BOOST_CHECK(pwallet->GetKeyFromPool(demoPubkey,false));
    CBitcoinAddress demoAddress = CBitcoinAddress(CTxDestination(demoPubkey.GetID()));
    Value retValue;
    string strAccount = "walletDemoAccount";
    BOOST_CHECK_NO_THROW({ /*Initialize Wallet with an account */
        CAccount account;
        account.vchPubKey = demoPubkey;
        pwallet->SetAddressLabel(account.vchPubKey.GetID(), strAccount);
    });

    CPubKey setaccountDemoPubkey;
    BOOST_CHECK(pwallet->GetKeyFromPool(setaccountDemoPubkey,false));
    CBitcoinAddress setaccountDemoAddress = CBitcoinAddress(CTxDestination(setaccountDemoPubkey.GetID()));

    /*********************************
     * 			setaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("setaccount " + setaccountDemoAddress.ToString() + " nullaccount", pwallet));
    /* D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R is not owned by the test wallet. */
    BOOST_CHECK_THROW(CallRPC("setaccount D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R nullaccount", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("setaccount", pwallet), runtime_error);
    /* D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 (33 chars) is an illegal address (should be 34 chars) */
    BOOST_CHECK_THROW(CallRPC("setaccount D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 nullaccount", pwallet), runtime_error);

    /*********************************
     * 			listunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listunspent", pwallet));
    BOOST_CHECK_THROW(CallRPC("listunspent string", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 string", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 not_array", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 [] extra", pwallet), runtime_error);
    BOOST_CHECK_NO_THROW(r = CallRPC("listunspent 0 1 []", pwallet));
    BOOST_CHECK(r.get_array().empty());

    /*********************************
     * 		listreceivedbyaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress", pwallet));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0", pwallet));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress not_int", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 not_bool", pwallet), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0 true", pwallet));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 true extra", pwallet), runtime_error);

    /*********************************
     * 		listreceivedbyaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount", pwallet));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0", pwallet));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount not_int", pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 not_bool", pwallet), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0 true", pwallet));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 true extra", pwallet), runtime_error);

    /*********************************
     * 		getrawchangeaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getrawchangeaddress", pwallet));

    /*********************************
     * 		getnewaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress", pwallet));
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress getnewaddress_demoaccount", pwallet));

    /*********************************
     * 		getaccountaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress \"\"", pwallet));
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress accountThatDoesntExists", pwallet)); // Should generate a new account
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaccountaddress " + strAccount, pwallet));
    BOOST_CHECK(CBitcoinAddress(retValue.get_str()).Get() == demoAddress.Get());

    /*********************************
     * 			getaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaccount", pwallet), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getaccount " + demoAddress.ToString(), pwallet));

    /*********************************
     * 	signmessage + verifymessage
     *********************************/
    BOOST_CHECK_NO_THROW(retValue = CallRPC("signmessage " + demoAddress.ToString() + " mymessage", pwallet));
    BOOST_CHECK_THROW(CallRPC("signmessage", pwallet), runtime_error);
    /* Should throw error because this address is not loaded in the wallet */
    BOOST_CHECK_THROW(CallRPC("signmessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R mymessage", pwallet), runtime_error);

    /* missing arguments */
    BOOST_CHECK_THROW(CallRPC("verifymessage " + demoAddress.ToString(), pwallet), runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str(), pwallet), runtime_error);
    /* Illegal address */
    BOOST_CHECK_THROW(CallRPC("verifymessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 " + retValue.get_str() + " mymessage", pwallet), runtime_error);
    /* wrong address */
    BOOST_CHECK(CallRPC("verifymessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R " + retValue.get_str() + " mymessage", pwallet).get_bool() == false);
    /* Correct address and signature but wrong message */
    BOOST_CHECK(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str() + " wrongmessage", pwallet).get_bool() == false);
    /* Correct address, message and signature*/
    BOOST_CHECK(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str() + " mymessage", pwallet).get_bool() == true);

    /*********************************
     * 		getaddressesbyaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaddressesbyaccount", pwallet), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaddressesbyaccount " + strAccount, pwallet));
    Array arr = retValue.get_array();
    BOOST_CHECK(arr.size() > 0);
    BOOST_CHECK(CBitcoinAddress(arr[0].get_str()).Get() == demoAddress.Get());

}


BOOST_AUTO_TEST_SUITE_END()
