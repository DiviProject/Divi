// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "core_io.h"
#include <chain.h>
#include <ChainstateManager.h>
#include "init.h"
#include <rpcprotocol.h>
#include "rpcserver.h"
#include "timedata.h"
#include "Logging.h"
#include "utiltime.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "masternode-payments.h"
#include <SuperblockSubsidyContainer.h>
#include <script/standard.h>
#include <script/StakingVaultScript.h>
#include <TransactionDiskAccessor.h>
#include <txmempool.h>
#include <net.h>
#include <WalletTx.h>
#include <stdint.h>
#include <OutputEntry.h>
#include <I_MerkleTxConfirmationNumberCalculator.h>
#include <I_CoinSelectionAlgorithm.h>
#include <MinimumFeeCoinSelectionAlgorithm.h>
#include <SignatureSizeEstimator.h>
#include <FeeAndPriorityCalculator.h>
#include <numeric>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include "spork.h"
#include <boost/assign/list_of.hpp>
#include <blockmap.h>
#include <BlockDiskAccessor.h>
#include <obfuscation.h>
#include <Settings.h>

#include <walletdb.h>

#include <JsonTxHelpers.h>

using namespace std;
using namespace boost;
using namespace boost::assign;
using namespace json_spirit;

extern CCriticalSection cs_main;
extern CWallet* pwalletMain;
extern Settings& settings;
extern CScript _createmultisig_redeemScript(const Array& params);

struct WalletOutputEntryParsing
{
    std::list<COutputEntry> listReceived;
    std::list<COutputEntry> listSent;
    CAmount nFee;
    CAmount sentByOthers;
    bool debitsFromAccount;
    std::vector<CTxOut> previousOutputsSpent;
};

bool DebitsFromAccount(const AddressBook& addressBook,const CTxOut& txout, const std::string accountName)
{
    CTxDestination address;
    if (ExtractDestination(txout.scriptPubKey, address) &&
        addressBook.count(address) > 0 &&
        addressBook.find(address)->second.name == accountName)
    {
        return true;
    }
    return false;
}
bool DebitsFromAnyAccount(const AddressBook& addressBook,const CTxOut& txout)
{
    CTxDestination address;
    if (ExtractDestination(txout.scriptPubKey, address) &&
        addressBook.count(address) > 0 &&
        !addressBook.find(address)->second.name.empty())
    {
        return true;
    }
    return false;
}

WalletOutputEntryParsing GetAmounts(
    const CWallet& wallet,
    const CWalletTx& wtx,
    const UtxoOwnershipFilter& filter,
    bool collectPreviousOutputs = false)
{
    WalletOutputEntryParsing parsedEntry;
    parsedEntry.debitsFromAccount = false;
    parsedEntry.nFee = 0;
    parsedEntry.sentByOthers = 0;
    parsedEntry.listReceived.clear();
    parsedEntry.listSent.clear();
    parsedEntry.previousOutputsSpent.clear();

    CAmount nDebit = wallet.getDebit(wtx,filter);
    wtx.totalInputs = 0;
    if(nDebit > 0)
    {
        if(wallet.AllInputsAreMine(wtx))
        {
            parsedEntry.nFee = nDebit - wtx.GetValueOut();
            wtx.totalInputs = nDebit;
            if(collectPreviousOutputs)
            {
                for(const CTxIn& txin: wtx.vin)
                {
                    const CWalletTx* otherWtx = wallet.GetWalletTx(txin.prevout.hash);
                    assert(otherWtx);
                    parsedEntry.previousOutputsSpent.push_back(otherWtx->vout[txin.prevout.n]);
                }
            }
        }
        else
        {
            CAmount totalInputs = 0;

            for(const CTxIn& txin: wtx.vin)
            {
                uint256 hashBlock;
                CTransaction tx;
                assert(GetTransaction(txin.prevout.hash,tx,hashBlock,true));
                if(collectPreviousOutputs) parsedEntry.previousOutputsSpent.push_back(tx.vout[txin.prevout.n]);
                totalInputs += tx.vout[txin.prevout.n].nValue;
            }
            wtx.totalInputs = totalInputs;
            parsedEntry.nFee = wtx.totalInputs - wtx.GetValueOut();
        }
    }
    parsedEntry.sentByOthers = wtx.totalInputs - nDebit;

    // Sent/received.
    for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
        const CTxOut& txout = wtx.vout[i];
        const bool skippedByFilter = !(filter.hasRequested(wallet.isMine(txout)));
        if (nDebit > 0 && wallet.IsChange(txout)) continue;
        if (!(nDebit > 0) && skippedByFilter) continue;

        // In either case, we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address)) {
            if (!wtx.IsCoinStake() && !wtx.IsCoinBase()) {
                LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n", wtx.ToStringShort());
            }
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            parsedEntry.listSent.push_back(output);

        // If we are receiving the output, add it as a "received" entry
        if (!skippedByFilter)
            parsedEntry.listReceived.push_back(output);

    }
    return parsedEntry;
}

void GetAccountAmounts(
    const CWallet& wallet,
    const CWalletTx& wtx,
    const std::string& strAccount,
    CAmount& nReceived,
    CAmount& nSent,
    CAmount& nFee,
    const UtxoOwnershipFilter& filter)
{
    nReceived = nSent = nFee = 0;

    std::string strSentAccount = wtx.strFromAccount;
    WalletOutputEntryParsing parsedEntry = GetAmounts(wallet,wtx, filter,true);
    const AddressBook& addressBook = wallet.GetAddressBookManager().GetAddressBook();
    if(!strAccount.empty())
    {
        // Spend from account
        for (const CTxOut& s : parsedEntry.previousOutputsSpent)
        {
            if(DebitsFromAccount(addressBook,s,strAccount))
            {
                nSent += s.nValue;
            }
        }
        // Receive to account
        for (const CTxOut& s : wtx.vout)
        {
            if(DebitsFromAccount(addressBook,s,strAccount))
            {
                nReceived += s.nValue;
            }
        }
        parsedEntry.nFee = 0;
        return;
    }
    else
    {
        // Spend from account
        for (const CTxOut& s : parsedEntry.previousOutputsSpent)
        {
            if(!DebitsFromAnyAccount(addressBook,s))
            {
                nSent += s.nValue;
            }
        }
        // Receive to account
        for (const CTxOut& s : wtx.vout)
        {
            if(!DebitsFromAnyAccount(addressBook,s))
            {
                nReceived += s.nValue;
            }
        }
        parsedEntry.nFee = 0;
        return;
    }
}

void WalletTxToJSON(const CWallet& wallet, const CWalletTx& wtx, Object& entry)
{

    int confirms = wallet.getConfirmationCalculator().GetNumberOfBlockConfirmations(wtx);
    int confirmsTotal = confirms;
    entry.push_back(Pair("confirmations", confirmsTotal));
    entry.push_back(Pair("bcconfirmations", confirms));
    if (wtx.IsCoinBase() || wtx.IsCoinStake())
        entry.push_back(Pair("generated", true));
    if (confirms > 0) {
        const ChainstateManager::Reference chainstate;
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.merkleBranchIndex));
        entry.push_back(Pair("blocktime", chainstate->GetBlockMap().at(wtx.hashBlock)->GetBlockTime()));
    }
    uint256 hash = wtx.GetHash();
    entry.push_back(Pair("txid", hash.GetHex()));
    entry.push_back(Pair("baretxid", wtx.GetBareTxid().GetHex()));
    Array conflicts;
    if(pwalletMain)
    {
        BOOST_FOREACH (const uint256& conflict, pwalletMain->GetConflicts(hash))
                conflicts.push_back(conflict.GetHex());
    }
    entry.push_back(Pair("walletconflicts", conflicts));
    entry.push_back(Pair("time", wtx.GetTxTime()));
    entry.push_back(Pair("timereceived", (int64_t)wtx.nTimeReceived));
    BOOST_FOREACH (const PAIRTYPE(string, string) & item, wtx.mapValue)
            entry.push_back(Pair(item.first, item.second));
}

string AccountFromValue(const Value& value)
{
    string strAccount = value.get_str();
    if (strAccount == "*")
        throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
    return strAccount;
}

Value getnewaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "getnewaddress ( \"account\" )\n"
                "\nReturns a new DIVI address for receiving payments.\n"
                "If 'account' is specified (recommended), it is added to the address book \n"
                "so payments received with the address will be credited to 'account'.\n"
                "\nArguments:\n"
                "1. \"account\"        (string, optional) The account name for the address to be linked to. if not provided, the default account \"\" is used. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created if there is no account by the given name.\n"
                "\nResult:\n"
                "\"diviaddress\"    (string) The new divi address\n"
                "\nExamples:\n" +
                HelpExampleCli("getnewaddress", "") + HelpExampleCli("getnewaddress", "\"\"") + HelpExampleCli("getnewaddress", "\"myaccount\"") + HelpExampleRpc("getnewaddress", "\"myaccount\""));

    // Parse the account first so we don't generate a key if there's an error
    string strAccount;
    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey, false))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    pwalletMain->SetAddressLabel(keyID, strAccount);

    return CBitcoinAddress(keyID).ToString();
}

bool AccountShouldUseNewKey(CWallet& wallet, const CScript& accountScript)
{
    if (!accountScript.empty())
    {
        std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
        for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
        {
            const CWalletTx& wtx = *(*it);
            BOOST_FOREACH (const CTxOut& txout, wtx.vout)
            {
                if (txout.scriptPubKey == accountScript)
                {
                    return true;
                }
            }
        }
        return false;
    }
    else
    {
        return true;
    }
}

CBitcoinAddress GetAccountAddress(CWallet& wallet, string strAccount, bool forceNewKey, bool isWalletDerivedKey)
{
    const auto& lastDestinationByLabel = wallet.GetAddressBookManager().GetLastDestinationByLabel();
    auto it = lastDestinationByLabel.find(strAccount);
    CScript destinationScript = (it != lastDestinationByLabel.end())? GetScriptForDestination(it->second) : CScript();
    CPubKey newlyCreatedPubKey;
    if (forceNewKey || AccountShouldUseNewKey(wallet, destinationScript))
    {// Generate a new key
        if (isWalletDerivedKey && !wallet.GetKeyFromPool(newlyCreatedPubKey, false))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        if(!isWalletDerivedKey)
        {
            CKey key;
            key.MakeNewKey(true);
            newlyCreatedPubKey = key.GetPubKey();
            {
                LOCK(wallet.cs_wallet);
                wallet.AddKeyPubKey(key,newlyCreatedPubKey);
            }
        }

        wallet.SetAddressLabel(newlyCreatedPubKey.GetID(), strAccount);
    }
    else
    {
        assert(!destinationScript.empty());
        CTxDestination destination;
        if(!ExtractDestination(destinationScript,destination))
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Error: invalid address with known label!");
        }
        return CBitcoinAddress(destination);
    }

    return CBitcoinAddress(newlyCreatedPubKey.GetID());
}

CBitcoinAddress GetAccountAddress(CWallet& wallet, string strAccount, bool bForceNew = false)
{
    return GetAccountAddress(wallet,strAccount,bForceNew,true);
}


CAmount GetAccountBalance(const string& strAccount, int nMinDepth, const UtxoOwnershipFilter& filter)
{
    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    CAmount nBalance = 0;

    // Tally wallet transactions
    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = pwalletMain->getConfirmationCalculator();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        const CWalletTx& wtx = *(*it);
        if (!IsFinalTx(wtx, chain) || confsCalculator.GetBlocksToMaturity(wtx) > 0 || confsCalculator.GetNumberOfBlockConfirmations(wtx) < 0)
            continue;

        CAmount nReceived, nSent, nFee;
        GetAccountAmounts(*pwalletMain,wtx,strAccount, nReceived, nSent, nFee, filter);

        if (nReceived != 0 && confsCalculator.GetNumberOfBlockConfirmations(wtx) >= nMinDepth)
            nBalance += nReceived;
        nBalance -= nSent + nFee;
    }

    // Tally internal accounting entries

    return nBalance;
}

class I_AccountCoinSelector: public I_CoinSelectionAlgorithm
{
protected:
    std::unique_ptr<I_CoinSelectionAlgorithm> coinSelector_;
    virtual std::vector<COutput> FilterCoinsByAccountName(const std::vector<COutput>& vCoins) const = 0;
public:
    virtual ~I_AccountCoinSelector(){}
    bool isSelectable(const COutput& coin) const final
    {
        return coinSelector_ && coinSelector_->isSelectable(coin) && !(FilterCoinsByAccountName({coin}).empty());
    }
    std::set<COutput> SelectCoins(
        const CMutableTransaction& transactionToSelectCoinsFor,
        const std::vector<COutput>& vCoins,
        CAmount& fees) const override final
    {
        std::vector<COutput> accountCoins = FilterCoinsByAccountName(vCoins);
        return coinSelector_? coinSelector_->SelectCoins(transactionToSelectCoinsFor,accountCoins,fees) : std::set<COutput>();
    }
};

class AccountCoinSelector final: public I_AccountCoinSelector
{
private:
    SignatureSizeEstimator signatureSizeEstimator_;
    const AddressBook& addressBook_;
    std::string accountName_;

    std::vector<COutput> FilterCoinsByAccountName(const std::vector<COutput>& vCoins) const override
    {
        std::vector<COutput> accountCoins;
        accountCoins.reserve(vCoins.size());
        for(const COutput& out: vCoins)
        {
            if( (!accountName_.empty() && DebitsFromAccount(addressBook_,out.tx->vout[out.i],accountName_)) ||
                (accountName_.empty() && !DebitsFromAnyAccount(addressBook_,out.tx->vout[out.i]) ))
            {
                accountCoins.push_back(out);
            }
        }
        return accountCoins;
    }
public:
    AccountCoinSelector(
        const CWallet& wallet
        ): signatureSizeEstimator_()
        , addressBook_(wallet.GetAddressBookManager().GetAddressBook())
        , accountName_()
    {
        coinSelector_.reset(
            new MinimumFeeCoinSelectionAlgorithm(
                wallet,
                signatureSizeEstimator_,
                FeeAndPriorityCalculator::instance().getMinimumRelayFeeRate()));
    }
    ~AccountCoinSelector(){ coinSelector_.reset(); }

    void SetAccountName(std::string accountName)
    {
        accountName_ = accountName;
    }
};





Value getaccountaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaccountaddress \"account\"\n"
                "\nReturns the current DIVI address for receiving payments to this account.\n"
                "\nArguments:\n"
                "1. \"account\"       (string, required) The account name for the address. It can also be set to the empty string \"\" to represent the default account. The account does not need to exist, it will be created and a new address created  if there is no account by the given name.\n"
                "\nResult:\n"
                "\"diviaddress\"   (string) The account divi address\n"
                "\nExamples:\n" +
                HelpExampleCli("getaccountaddress", "") + HelpExampleCli("getaccountaddress", "\"\"") + HelpExampleCli("getaccountaddress", "\"myaccount\"") + HelpExampleRpc("getaccountaddress", "\"myaccount\""));

    // Parse the account first so we don't generate a key if there's an error
    string strAccount = AccountFromValue(params[0]);

    return GetAccountAddress(*pwalletMain, strAccount).ToString();
}


Value getrawchangeaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "getrawchangeaddress\n"
                "\nReturns a new DIVI address, for receiving change.\n"
                "This is for use with raw transactions, NOT normal use.\n"
                "\nResult:\n"
                "\"address\"    (string) The address\n"
                "\nExamples:\n" +
                HelpExampleCli("getrawchangeaddress", "") + HelpExampleRpc("getrawchangeaddress", ""));

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    CReserveKey reservekey(*pwalletMain);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey, true))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    return CBitcoinAddress(keyID).ToString();
}


Value setaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "setaccount \"diviaddress\" \"account\"\n"
                "\nSets the account associated with the given address.\n"
                "\nArguments:\n"
                "1. \"diviaddress\"  (string, required) The divi address to be associated with an account.\n"
                "2. \"account\"         (string, required) The account to assign the address to.\n"
                "\nExamples:\n" +
                HelpExampleCli("setaccount", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" \"tabby\"") + HelpExampleRpc("setaccount", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", \"tabby\""));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");


    string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Only add the account if the address is yours.
    if (pwalletMain->isMine(address.Get()) != isminetype::ISMINE_NO ) {
        // Detect when changing the account of an address that is the 'unused current key' of another account:
        const AddressBook& addressBook = pwalletMain->GetAddressBookManager().GetAddressBook();
        if (addressBook.count(address.Get())) {
            string strOldAccount = addressBook.find(address.Get())->second.name;
            if (address == GetAccountAddress(*pwalletMain,strOldAccount))
                GetAccountAddress(*pwalletMain,strOldAccount, true);
        }
        pwalletMain->SetAddressLabel(address.Get(), strAccount);
    } else
        throw JSONRPCError(RPC_MISC_ERROR, "setaccount can only be used with own address");

    return Value::null;
}


Value getaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaccount \"diviaddress\"\n"
                "\nReturns the account associated with the given address.\n"
                "\nArguments:\n"
                "1. \"diviaddress\"  (string, required) The divi address for account lookup.\n"
                "\nResult:\n"
                "\"accountname\"        (string) the account address\n"
                "\nExamples:\n" +
                HelpExampleCli("getaccount", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"") + HelpExampleRpc("getaccount", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\""));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");

    string strAccount;
    const AddressBook& addressBook = pwalletMain->GetAddressBookManager().GetAddressBook();
    AddressBook::const_iterator it = addressBook.find(address.Get());
    if (it != addressBook.end() && !(*it).second.name.empty())
        strAccount = (*it).second.name;
    return strAccount;
}


Value getaddressesbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaddressesbyaccount \"account\"\n"
                "\nReturns the list of addresses for the given account.\n"
                "\nArguments:\n"
                "1. \"account\"  (string, required) The account name.\n"
                "\nResult:\n"
                "[                     (json array of string)\n"
                "  \"diviaddress\"  (string) a divi address associated with the given account\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n" +
                HelpExampleCli("getaddressesbyaccount", "\"tabby\"") + HelpExampleRpc("getaddressesbyaccount", "\"tabby\""));

    string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    Array ret;
    BOOST_FOREACH (const PAIRTYPE(CBitcoinAddress, AddressLabel) & item, pwalletMain->GetAddressBookManager().GetAddressBook()) {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount)
            ret.push_back(address.ToString());
    }
    return ret;
}

struct RpcTransactionCreationRequest
{
    bool txShouldSpendFromVaults;
    TxTextMetadata txMetadata;
    TransactionFeeMode txFeeMode;
    std::string accountName;
    RpcTransactionCreationRequest(
        bool spendFromVaults = false,
        TxTextMetadata metadata = TxTextMetadata(),
        TransactionFeeMode feeMode = TransactionFeeMode::SENDER_PAYS_FOR_TX_FEES,
        std::string accountNameToDrawFrom = ""
        ): txShouldSpendFromVaults(spendFromVaults)
        , txMetadata(metadata)
        , txFeeMode(feeMode)
        , accountName(accountNameToDrawFrom)
    {
    }

    AvailableCoinsType coinType()
    {
        return (!txShouldSpendFromVaults)? AvailableCoinsType::ALL_SPENDABLE_COINS: AvailableCoinsType::OWNED_VAULT_COINS;
    }
};

std::string SendMoney(
    const std::vector<std::pair<CScript,CAmount>>& scriptsToFund,
    RpcTransactionCreationRequest rpcTxRequest)
{
    // Check amount
    const bool useDefaultAccount = rpcTxRequest.accountName.empty();
    assert(useDefaultAccount || !rpcTxRequest.txShouldSpendFromVaults);

    CAmount availableWalletBalance = 0;
    if(useDefaultAccount)
    {
        availableWalletBalance = rpcTxRequest.txShouldSpendFromVaults
            ? pwalletMain->GetVaultedBalance()
            : pwalletMain->GetSpendableBalance();
    }
    else
    {
        const int nMinDepth = 1;
        availableWalletBalance = GetAccountBalance(rpcTxRequest.accountName, nMinDepth, isminetype::ISMINE_SPENDABLE);
    }
    const CAmount totalAmountToSend = std::accumulate(scriptsToFund.begin(),scriptsToFund.end(),CAmount(0),
        [](const CAmount& runningTotal, const std::pair<CScript,CAmount>& recipient)
        {
            return runningTotal + recipient.second;
        });
    if (totalAmountToSend <= 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");
    }
    else if(totalAmountToSend > availableWalletBalance)
    {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");
    }
    else if (pwalletMain->IsLocked())
    {
        std::string strError = "Error: Wallet locked, unable to create transaction!";
        LogPrintf("SendMoney() : %s", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    static AccountCoinSelector coinSelector(*pwalletMain);
    coinSelector.SetAccountName(rpcTxRequest.accountName);
    TransactionCreationRequest request(scriptsToFund,rpcTxRequest.txFeeMode, rpcTxRequest.txMetadata, rpcTxRequest.coinType(), &coinSelector);
    TransactionCreationResult txCreation = pwalletMain->SendMoney(request);
    if(txCreation.transactionCreationSucceeded)
    {
        // Broadcast
        {
            LOCK(cs_main);
            if (!SubmitTransactionToMempool(GetTransactionMemoryPool(), *txCreation.wtxNew)) {
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
                std::string strError = "Error: Invalid transaction committed to wallet!";
                throw JSONRPCError(RPC_WALLET_ERROR, strError);
            }
        }
        if(!txCreation.wtxNew->IsCoinBase() && !txCreation.wtxNew->IsCoinStake())
        {
            LogPrintf("Relaying wtx %s\n", txCreation.wtxNew->ToStringShort());
            RelayTransactionToAllPeers(static_cast<CTransaction>(*txCreation.wtxNew));
        }
        else
        {
            std::string strError = "Error: Cannot relay blockreward transaction!";
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
    }
    else
    {
        std::string strError = txCreation.errorMessage;
        LogPrintf("SendMoney() : %s\n", strError);
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    return txCreation.wtxNew->GetHash().GetHex();
}

std::string SendMoneyToAddress(const CTxDestination& address, CAmount nValue, RpcTransactionCreationRequest rpcRequest)
{
    // Parse DIVI address
    CScript scriptPubKey = GetScriptForDestination(address);
    return SendMoney({std::make_pair(scriptPubKey, nValue)}, rpcRequest);
}

std::string SendMoneyToAddress(const CTxDestination& address, CAmount nValue)
{
    RpcTransactionCreationRequest rpcRequest;
    return SendMoneyToAddress(address,nValue, rpcRequest);
}

Value getcoinavailability(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error("");

    Object result;
    if(!pwalletMain)
    {
        result.push_back(Pair("Error","No wallet available at this time"));
    }
    bool verbose = (params.size()<1)? false:params[0].get_bool();
    if(!verbose)
    {
        auto outputAmountAdder = [](const std::vector<COutput>& outputsToTotal)
            {
                CAmount totalAmount = 0;
                for(const COutput& output: outputsToTotal)
                {
                    totalAmount += output.Value();
                }
                return totalAmount;
            };
        std::vector<COutput> outputs;
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::OWNED_VAULT_COINS);
        result.push_back( Pair("Vaulted", ValueFromAmount(outputAmountAdder(outputs))  ) );
        outputs.clear();
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::STAKABLE_COINS);
        result.push_back( Pair("Stakable", ValueFromAmount(outputAmountAdder(outputs)) ) );
        outputs.clear();
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::ALL_SPENDABLE_COINS);
        result.push_back( Pair("Spendable", ValueFromAmount(outputAmountAdder(outputs)) ) );

        return result;
    }
    else
    {
        auto outputParser = [](const std::vector<COutput>& outputsToTotal)
            {
                Object description;
                std::map<CScript, CAmount> valueByScript;
                std::map<CScript, std::set<std::string>> txdata;
                CAmount totalAmount = 0;
                CAmount totalVaultOnlyAmount = 0;
                for(const COutput& output: outputsToTotal)
                {
                    CTxOut txout = output.tx->vout[output.i];
                    valueByScript[txout.scriptPubKey] += txout.nValue;
                    txdata[txout.scriptPubKey].insert(output.tx->GetHash().ToString());
                    totalAmount += output.Value();
                }
                Array vaults;
                for(const std::pair<CScript,CAmount>& fundedScript: valueByScript)
                {
                    Object scriptResult;
                    if(IsStakingVaultScript(fundedScript.first))
                    {
                        std::pair<valtype,valtype> pubkeyHashesForVault;
                        if(!GetStakingVaultPubkeyHashes(fundedScript.first,pubkeyHashesForVault))
                        {
                            throw JSONRPCError(RPC_PARSE_ERROR,"Staking vault script unable to recover pubkey hashes!");
                        }
                        CKeyID ownerKeyID(uint160(pubkeyHashesForVault.first));
                        CKeyID managerKeyID(uint160(pubkeyHashesForVault.second));
                        CBitcoinAddress ownerAddress;
                        CBitcoinAddress managerAddress;
                        ownerAddress.Set(ownerKeyID);
                        managerAddress.Set(managerKeyID);

                        std::string vaultEncoding = ownerAddress.ToString() + ":"+ managerAddress.ToString();

                        scriptResult.push_back(Pair("vault",vaultEncoding));
                        scriptResult.push_back(Pair("value",ValueFromAmount(fundedScript.second) ));

                        Array txhashes;
                        for(const auto& txHashString: txdata[fundedScript.first])
                        {
                            txhashes.push_back(txHashString);
                        }
                        scriptResult.push_back(Pair("txids",txhashes));

                        vaults.push_back(scriptResult);
                        totalVaultOnlyAmount += fundedScript.second;
                    }
                }
                description.push_back(Pair("AllVaults",vaults));
                description.push_back(Pair("NonVaults",ValueFromAmount(totalAmount - totalVaultOnlyAmount)));
                return description;
            };
        std::vector<COutput> outputs;
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::OWNED_VAULT_COINS);
        result.push_back( Pair("Vaulted", outputParser(outputs)  ) );
        outputs.clear();
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::STAKABLE_COINS);
        result.push_back( Pair("Stakable", outputParser(outputs) ) );
        outputs.clear();
        pwalletMain->AvailableCoins(outputs, true,  AvailableCoinsType::ALL_SPENDABLE_COINS);
        result.push_back( Pair("Spendable", outputParser(outputs) ) );

        return result;
    }
}

Value fundvault(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
                "fundvault \"[owner_address:]manager_address\" amount\n"
                "\nSend an amount to a given vault manager address. The amount is a real and is rounded to the nearest 0.00000001\n" +
                HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"[owner_address:]manager_address\" (string, required)\n"
                "   \"owner_address\" -> The address of the key owning the vault funds. Needs ':' separator if used. \n"
                "   \"manager_address\" -> The divi address owned by the vault manager.\n"
                "2. \"amount\"      (numeric, required) The amount in DIVI to send. eg 0.1\n");

    std::string addressEncodings = params[0].get_str();
    CBitcoinAddress ownerAddress;
    CBitcoinAddress managerAddress;
    size_t indexOfSeparator = addressEncodings.find(':');
    if(indexOfSeparator != std::string::npos)
    {
        ownerAddress.SetString(addressEncodings.substr(0u,indexOfSeparator));
        managerAddress.SetString(addressEncodings.substr(indexOfSeparator+1));
    }
    else
    {
        string strAccount = AccountFromValue("");
        if (!pwalletMain->IsLocked())
            pwalletMain->TopUpKeyPool();

        // Generate a new key that is added to wallet
        CPubKey ownerKey;
        if (!pwalletMain->GetKeyFromPool(ownerKey, false))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
        CKeyID ownerKeyID = ownerKey.GetID();
        pwalletMain->SetAddressLabel(ownerKeyID, strAccount);

        ownerAddress.Set(ownerKeyID);
        managerAddress.SetString(addressEncodings);

        addressEncodings = ownerAddress.ToString() + ":" + managerAddress.ToString();
    }

    CKeyID managerKeyID;
    if (!managerAddress.IsValid() || !managerAddress.GetKeyID(managerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Funding failed: Invalid manager DIVI address");

    CKeyID ownerKeyID;
    if (!ownerAddress.IsValid() || !ownerAddress.GetKeyID(ownerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Funding failed: Invalid owner DIVI address");

    CScript vaultScript = CreateStakingVaultScript(ToByteVector(ownerKeyID),ToByteVector(managerKeyID));

    EnsureWalletIsUnlocked();
    // Amount & Send
    CAmount nAmount = AmountFromValue(params[1]);
    RpcTransactionCreationRequest rpcRequest;
    rpcRequest.txShouldSpendFromVaults = false;
    const std::string txid = SendMoney({std::make_pair(vaultScript, nAmount)}, rpcRequest);

    Object fundingAttemptResult;
    fundingAttemptResult.push_back(Pair("txhash", txid ));
    fundingAttemptResult.push_back(Pair("vault",addressEncodings));
    fundingAttemptResult.push_back(Pair("script",HexStr(ToByteVector(vaultScript) ) ));
    return fundingAttemptResult;
}


Value reclaimvaultfunds(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
                "reclaimvaultfunds destination amount (feeMode|metadata) ( \"comment\" \"comment-to\" )\n"
                "\nWithdraw an amount from your vaults into a separate address. The amount is a real and is rounded to the nearest 0.00000001\n" +
                HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"diviaddress\"  (string, required) The divi address of your choosing to send to.\n"
                "2. \"amount\"      (numeric, required) The amount in DIVI to move. eg 0.1\n"
                "\nResult:\n"
                "\"transactionid\"  (string) The transaction id.\n");

    // Address
    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");
    // Amount
    CAmount nAmount = AmountFromValue(params[1]);

    RpcTransactionCreationRequest rpcRequest;
    rpcRequest.txShouldSpendFromVaults = true;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
    {
        const std::string feeMode = params[2].get_str();
        if(feeMode == std::string("receiver_pays"))
        {
            rpcRequest.txFeeMode = TransactionFeeMode::RECEIVER_PAYS_FOR_TX_FEES;
        }
        else if(feeMode == std::string("sweep_funds"))
        {
            rpcRequest.txFeeMode = TransactionFeeMode::SWEEP_FUNDS;
        }
        else if(feeMode == std::string("metadata"))
        {
        // Wallet comments
            if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
                rpcRequest.txMetadata["comment"] = params[3].get_str();
            if (params.size() > 4 && params[4].type() != null_type && !params[4].get_str().empty())
                rpcRequest.txMetadata["to"] = params[4].get_str();
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Unknown option for reclaiming vault funds");
        }
    }



    EnsureWalletIsUnlocked();
    return SendMoneyToAddress(address.Get(), nAmount, rpcRequest);
}

Value removevault(const Array& params, bool fHelp)
{
     if (fHelp || params.size() != 1)
        throw runtime_error(
                "removevault \"<owner_address>:<manager_address>\"\n"
                "\nAllows vault manager to reject staking the indicated vault script.\n"
                "\nArguments:\n"
                "1. \"<owner_address>:<manager_address>\"  (string, required) Vault representation as a pair of addresses.\n"
                "1. \"tx_hash\"  (string, required) The transaction hash to search for the initial funding.\n");

    Object result;

    std::string addressEncodings = params[0].get_str();
    CBitcoinAddress ownerAddress;
    CBitcoinAddress managerAddress;
    size_t indexOfSeparator = addressEncodings.find(':');
    if(indexOfSeparator != std::string::npos)
    {
        ownerAddress.SetString(addressEncodings.substr(0u,indexOfSeparator));
        managerAddress.SetString(addressEncodings.substr(indexOfSeparator+1));
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not parse vault encoding!");
    }

    CKeyID managerKeyID;
    if (!managerAddress.IsValid() || !managerAddress.GetKeyID(managerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Vault Registry Failed: Invalid manager DIVI address");

    CKeyID ownerKeyID;
    if (!ownerAddress.IsValid() || !ownerAddress.GetKeyID(ownerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Vault Registry Failed: Invalid owner DIVI address");

    CScript script = CreateStakingVaultScript(ToByteVector(ownerKeyID),ToByteVector(managerKeyID));
    result.push_back(Pair("removal_status", pwalletMain->RemoveVault(script) ));
    return result;
}

Value addvault(const Array& params, bool fHelp)
{
     if (fHelp || params.size() != 2)
        throw runtime_error(
                "addvault \"<owner_address>:<manager_address>\" funding_txhash\n"
                "\nAllows vault manager to accept to stake the indicated vault script.\n"
                "\nArguments:\n"
                "1. \"<owner_address>:<manager_address>\"  (string, required) Vault representation as a pair of addresses.\n"
                "1. \"tx_hash\"  (string, required) The transaction hash to search for the initial funding.\n");

    Object result;

    std::string addressEncodings = params[0].get_str();
    CBitcoinAddress ownerAddress;
    CBitcoinAddress managerAddress;
    size_t indexOfSeparator = addressEncodings.find(':');
    if(indexOfSeparator != std::string::npos)
    {
        ownerAddress.SetString(addressEncodings.substr(0u,indexOfSeparator));
        managerAddress.SetString(addressEncodings.substr(indexOfSeparator+1));
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not parse vault encoding!");
    }

    CKeyID managerKeyID;
    if (!managerAddress.IsValid() || !managerAddress.GetKeyID(managerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Vault Registry Failed: Invalid manager DIVI address");

    CKeyID ownerKeyID;
    if (!ownerAddress.IsValid() || !ownerAddress.GetKeyID(ownerKeyID))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Vault Registry Failed: Invalid owner DIVI address");

    if(!pwalletMain->HaveKey(managerKeyID))
        throw JSONRPCError(RPC_INVALID_REQUEST, "AddingVaultScript: Does not have matching manager key!");

    uint256 txhash = uint256(params[1].get_str());
    CTransaction tx;
    uint256 blockHash;
    if(!GetTransaction(txhash,tx,blockHash,true))
    {
        LogPrintf("AddingVaultScript: Failed to find tx when adding vault script!\n");
        result.push_back(Pair("succeeded", false));
        return result;
    }

    const ChainstateManager::Reference chainstate;
    const CBlockIndex* blockSearchStart = chainstate->ActiveChain().Tip();
    while (blockSearchStart->pprev)
    {
        if(blockSearchStart->GetBlockHash() == blockHash) break;
        blockSearchStart = blockSearchStart->pprev;
    }
    if(!blockSearchStart->pprev)
    {
        LogPrintf("AddingVaultScript: Failed to find matching block index!\n");
        result.push_back(Pair("succeeded", false));
        return result;
    }

    CScript script = CreateStakingVaultScript(ToByteVector(ownerKeyID),ToByteVector(managerKeyID));
    CBlock block;
    if(!ReadBlockFromDisk(block,blockSearchStart))
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "AddingVaultScript: error reading block from disk");
    }
    if(!pwalletMain->AddVault(script,&block,tx))
    {
        throw JSONRPCError(RPC_INVALID_REQUEST, "AddingVaultScript: Unable to sync TX!");
    }
    result.push_back(Pair("succeeded", true));
    return result;
}

Value sendtoaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
                "sendtoaddress \"diviaddress\" amount (feeMode|metadata)( \"comment\" \"comment-to\" )\n"
                "\nSend an amount to a given address. The amount is a real and is rounded to the nearest 0.00000001\n" +
                HelpRequiringPassphrase() +
                "\nArguments:\n"
                "1. \"diviaddress\"  (string, required) The divi address to send to.\n"
                "2. \"amount\"      (numeric, required) The amount in DIVI to send. eg 0.1\n"
                "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
                "                             This is not part of the transaction, just kept in your wallet.\n"
                "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
                "                             to which you're sending the transaction. This is not part of the \n"
                "                             transaction, just kept in your wallet.\n"
                "\nResult:\n"
                "\"transactionid\"  (string) The transaction id.\n"
                "\nExamples:\n" +
                HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.1") + HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.1 \"donation\" \"seans outpost\"") + HelpExampleRpc("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", 0.1, \"donation\", \"seans outpost\""));

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);

    // Wallet comments
    RpcTransactionCreationRequest rpcRequest;
    rpcRequest.txShouldSpendFromVaults = false;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
    {
        const std::string feeMode = params[2].get_str();
        if(feeMode == std::string("receiver_pays"))
        {
            rpcRequest.txFeeMode = TransactionFeeMode::RECEIVER_PAYS_FOR_TX_FEES;
        }
        else if(feeMode == std::string("sweep_funds"))
        {
            rpcRequest.txFeeMode = TransactionFeeMode::SWEEP_FUNDS;
        }
        else if(feeMode == std::string("metadata"))
        {
            if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
                rpcRequest.txMetadata["comment"] = params[3].get_str();
            if (params.size() > 4 && params[4].type() != null_type && !params[4].get_str().empty())
                rpcRequest.txMetadata["to"] = params[4].get_str();
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER,"Unknown option for reclaiming vault funds");
        }
    }

    EnsureWalletIsUnlocked();
    return SendMoneyToAddress(address.Get(), nAmount, rpcRequest);
}

Value signmessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
                "signmessage \"diviaddress\" \"message\" \"input_format\" \"output_format\"\n"
                "\nSign a message with the private key of an address" +
                HelpRequiringPassphrase() + "\n"
                                            "\nArguments:\n"
                                            "1. \"diviaddress\"    (string, required) The divi address to use for the private key.\n"
                                            "2. \"message\"        (string, required) The message to create a signature of.\n"
                                            "3. \"input_format\"   (string, optional) ['hex'] Message encoding format. Default plaintext\n"
                                            "4. \"output_format\"  (string, optional) ['hex'] Message encoding format. Default base64\n"
                                            "\nResult:\n"
                                            "\"signature\"          (string) The signature of the message encoded in base 64\n"
                                            "\nExamples:\n"
                                            "\nUnlock the wallet for 30 seconds\n" +
                HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
                "\nCreate the signature\n" + HelpExampleCli("signmessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" \"my message\"") +
                "\nVerify the signature\n" + HelpExampleCli("verifymessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" \"signature\" \"my message\"") +
                "\nAs json rpc\n" + HelpExampleRpc("signmessage", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", \"my message\""));

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    string strMessage = params[1].get_str();
    if(params.size()>2)
    {
        string format = static_cast<string>(params[2].get_str());
        if(std::strcmp(format.c_str(),"hex")==0)
        {
            valtype decodedHex = ParseHex(strMessage);
            strMessage = std::string(decodedHex.begin(),decodedHex.end());
        }
        else if(std::strcmp(format.c_str(),"b64")==0)
        {
            strMessage = DecodeBase64(strMessage);
        }
    }
    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");

    std::vector<unsigned char> vchSig;
    std::string errMsg;
    if(!CObfuScationSigner::SignMessage(strMessage,errMsg,vchSig,key))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");
    }

    if(params.size()==4)
    {
        string outputFormat = static_cast<string>(params[3].get_str());
        if(std::strcmp(outputFormat.c_str(),"hex")==0)
        {
            return HexStr(vchSig);
        }
    }
    return EncodeBase64(&vchSig[0], vchSig.size());
}

Value getreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "getreceivedbyaddress \"diviaddress\" ( minconf )\n"
                "\nReturns the total amount received by the given diviaddress in transactions with at least minconf confirmations.\n"
                "\nArguments:\n"
                "1. \"diviaddress\"  (string, required) The divi address for transactions.\n"
                "2. minconf             (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "\nResult:\n"
                "amount   (numeric) The total amount in DIVI received at this address.\n"
                "\nExamples:\n"
                "\nThe amount from transactions with at least 1 confirmation\n" +
                HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\"") +
                "\nThe amount including unconfirmed transactions, zero confirmations\n" + HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0") +
                "\nThe amount with at least 6 confirmation, very safe\n" + HelpExampleCli("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 6") +
                "\nAs a json rpc call\n" + HelpExampleRpc("getreceivedbyaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", 6"));

    // divi address
    CBitcoinAddress address = CBitcoinAddress(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");
    if (pwalletMain->isMine(address.Get()) == isminetype::ISMINE_NO)
        return (double)0.0;

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // Tally
    CAmount nAmount = 0;
    CScript scriptPubKey = GetScriptForDestination(address.Get());
    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        const CWalletTx& wtx = *(*it);
        if (wtx.IsCoinBase() || !IsFinalTx(wtx, chain))
            continue;

        BOOST_FOREACH (const CTxOut& txout, wtx.vout)
                if (txout.scriptPubKey == scriptPubKey)
                if (pwalletMain->getConfirmationCalculator().GetNumberOfBlockConfirmations(wtx) >= nMinDepth)
                nAmount += txout.nValue;
    }

    return ValueFromAmount(nAmount);
}

std::set<CTxDestination> GetAccountAddresses(const AddressBook& addressBook, std::string strAccount)
{
    std::set<CTxDestination> result;
    BOOST_FOREACH (const PAIRTYPE(CTxDestination, AddressLabel) & item, addressBook)
    {
        const CTxDestination& address = item.first;
        const std::string& strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

Value getreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "getreceivedbyaccount \"account\" ( minconf )\n"
                "\nReturns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.\n"
                "\nArguments:\n"
                "1. \"account\"      (string, required) The selected account, may be the default account using \"\".\n"
                "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "\nResult:\n"
                "amount              (numeric) The total amount in DIVI received for this account.\n"
                "\nExamples:\n"
                "\nAmount received by the default account with at least 1 confirmation\n" +
                HelpExampleCli("getreceivedbyaccount", "\"\"") +
                "\nAmount received at the tabby account including unconfirmed amounts with zero confirmations\n" + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 0") +
                "\nThe amount with at least 6 confirmation, very safe\n" + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 6") +
                "\nAs a json rpc call\n" + HelpExampleRpc("getreceivedbyaccount", "\"tabby\", 6"));

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys assigned to account
    string strAccount = AccountFromValue(params[0]);
    std::set<CTxDestination> setAddress;
    {
        LOCK(pwalletMain->cs_wallet);
        setAddress = GetAccountAddresses(pwalletMain->GetAddressBookManager().GetAddressBook(),strAccount);
    }

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // Tally
    CAmount nAmount = 0;
    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        const CWalletTx& wtx = *(*it);
        if (wtx.IsCoinBase() || !IsFinalTx(wtx, chain))
            continue;

        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && pwalletMain->isMine(address) != isminetype::ISMINE_NO && setAddress.count(address))
                if (pwalletMain->getConfirmationCalculator().GetNumberOfBlockConfirmations(wtx) >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return (double)nAmount / (double)COIN;
}


Value getbalance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
                "getbalance ( \"account\" minconf includeWatchonly )\n"
                "\nIf account is not specified, returns the server's total available balance.\n"
                "If account is specified, returns the balance in the account.\n"
                "Note that the account \"\" is not the same as leaving the parameter out.\n"
                "The server total may be different to the balance in the default \"\" account.\n"
                "\nArguments:\n"
                "1. \"account\"      (string, optional) The selected account, or \"*\" for entire wallet. It may be the default account using \"\".\n"
                "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
                "3. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress')\n"
                "\nResult:\n"
                "amount              (numeric) The total amount in DIVI received for this account.\n"
                "\nExamples:\n"
                "\nThe total amount in the server across all accounts\n" +
                HelpExampleCli("getbalance", "") +
                "\nThe total amount in the server across all accounts, with at least 5 confirmations\n" + HelpExampleCli("getbalance", "\"*\" 6") +
                "\nThe total amount in the default account with at least 1 confirmation\n" + HelpExampleCli("getbalance", "\"\"") +
                "\nThe total amount in the account named tabby with at least 6 confirmations\n" + HelpExampleCli("getbalance", "\"tabby\" 6") +
                "\nAs a json rpc call\n" + HelpExampleRpc("getbalance", "\"tabby\", 6"));

    if (params.size() == 0 || params[0].get_str() == "*")
        return ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    if (params.size() > 2)
        if (params[2].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    std::string strAccount = AccountFromValue(params[0]);

    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, filter);

    return ValueFromAmount(nBalance);
}

Value getunconfirmedbalance(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
                "getunconfirmedbalance\n"
                "Returns the server's total unconfirmed balance\n");
    return ValueFromAmount(pwalletMain->GetUnconfirmedBalance());
}

Value sendfrom(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
                "sendfrom \"fromaccount\" \"todiviaddress\" amount ( \"comment\" \"comment-to\" )\n"
                "\nSent an amount from an account to a divi address.\n"
                "The amount is a real and is rounded to the nearest 0.00000001." +
                HelpRequiringPassphrase() + "\n"
                                            "\nArguments:\n"
                                            "1. \"fromaccount\"       (string, required) The name of the account to send funds from. May be the default account using \"\".\n"
                                            "2. \"todiviaddress\"  (string, required) The divi address to send funds to.\n"
                                            "3. amount                (numeric, required) The amount in DIVI. (transaction fee is added on top).\n"
                                            "4. \"comment\"           (string, optional) A comment used to store what the transaction is for. \n"
                                            "                                     This is not part of the transaction, just kept in your wallet.\n"
                                            "5. \"comment-to\"        (string, optional) An optional comment to store the name of the person or organization \n"
                                            "                                     to which you're sending the transaction. This is not part of the transaction, \n"
                                            "                                     it is just kept in your wallet.\n"
                                            "\nResult:\n"
                                            "\"transactionid\"        (string) The transaction id.\n"
                                            "\nExamples:\n"
                                            "\nSend 0.01 DIVI from the default account to the address, must have at least 1 confirmation\n" +
                HelpExampleCli("sendfrom", "\"\" \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.01") +
                "\nSend 0.01 from the tabby account to the given address, funds must have at least 6 confirmations\n" + HelpExampleCli("sendfrom", "\"tabby\" \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 0.01 6 \"donation\" \"seans outpost\"") +
                "\nAs a json rpc call\n" + HelpExampleRpc("sendfrom", "\"tabby\", \"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\", 0.01, 6, \"donation\", \"seans outpost\""));

    string strAccount = AccountFromValue(params[0]);
    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid DIVI address");
    CAmount nAmount = AmountFromValue(params[2]);

    TxTextMetadata metadata;
    metadata["FromAccount"] = strAccount;
    if (params.size() > 3 && params[3].type() != null_type && !params[3].get_str().empty())
        metadata["comment"] = params[3].get_str();
    if (params.size() > 4 && params[4].type() != null_type && !params[4].get_str().empty())
        metadata["to"] = params[4].get_str();

    EnsureWalletIsUnlocked();
    RpcTransactionCreationRequest rpcTxRequest;
    rpcTxRequest.txMetadata = metadata;
    rpcTxRequest.accountName = strAccount;
    return SendMoneyToAddress(address.Get(),nAmount,rpcTxRequest);
}


Value sendmany(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
                "sendmany \"fromaccount\" {\"address\":amount,...} ( \"comment\" )\n"
                "\nSend multiple times. Amounts are double-precision floating point numbers." +
                HelpRequiringPassphrase() + "\n"
                                            "\nArguments:\n"
                                            "1. \"fromaccount\"         (string, required) The account to send the funds from, can be \"\" for the default account\n"
                                            "2. \"amounts\"             (string, required) A json object with addresses and amounts\n"
                                            "    {\n"
                                            "      \"address\":amount   (numeric) The divi address is the key, the numeric amount in DIVI is the value\n"
                                            "      ,...\n"
                                            "    }\n"
                                            "3. \"comment\"             (string, optional) A comment\n"
                                            "\nResult:\n"
                                            "\"transactionid\"          (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
                                            "                                    the number of addresses.\n"
                                            "\nExamples:\n"
                                            "\nSend two amounts to two different addresses:\n" +
                HelpExampleCli("sendmany", "\"tabby\" \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\":0.02}\"") +
                "\nSend two amounts to two different addresses setting the confirmation and comment:\n" + HelpExampleCli("sendmany", "\"tabby\" \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\":0.02}\" 6 \"testing\"") +
                "\nAs a json rpc call\n" + HelpExampleRpc("sendmany", "\"tabby\", \"{\\\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\\\":0.01,\\\"XuQQkwA4FYkq2XERzMY2CiAZhJTEDAbtcg\\\":0.02}\", 6, \"testing\""));

    string strAccount = AccountFromValue(params[0]);
    Object sendTo = params[1].get_obj();
    TxTextMetadata metadata;
    metadata["FromAccount"] = strAccount;
    if (params.size() > 2 && params[2].type() != null_type && !params[2].get_str().empty())
        metadata["comment"] = params[2].get_str();

    set<CBitcoinAddress> setAddress;
    std::vector<std::pair<CScript, CAmount> > vecSend;

    CAmount totalAmount = 0;
    BOOST_FOREACH (const Pair& s, sendTo) {
        CBitcoinAddress address(s.name_);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid DIVI address: ") + s.name_);

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + s.name_);
        setAddress.insert(address);

        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount = AmountFromValue(s.value_);
        totalAmount += nAmount;

        vecSend.push_back(std::make_pair(scriptPubKey, nAmount));
    }

    EnsureWalletIsUnlocked();

    // Send
    RpcTransactionCreationRequest rpcTxRequest;
    rpcTxRequest.txMetadata = metadata;
    rpcTxRequest.accountName = strAccount;
    return SendMoney(vecSend,rpcTxRequest);
}

Value addmultisigaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3) {
        string msg = "addmultisigaddress nrequired [\"key\",...] ( \"account\" )\n"
                     "\nAdd a nrequired-to-sign multisignature address to the wallet.\n"
                     "Each key is a DIVI address or hex-encoded public key.\n"
                     "If 'account' is specified, assign address to that account.\n"

                     "\nArguments:\n"
                     "1. nrequired        (numeric, required) The number of required signatures out of the n keys or addresses.\n"
                     "2. \"keysobject\"   (string, required) A json array of divi addresses or hex-encoded public keys\n"
                     "     [\n"
                     "       \"address\"  (string) divi address or hex-encoded public key\n"
                     "       ...,\n"
                     "     ]\n"
                     "3. \"account\"      (string, optional) An account to assign the addresses to.\n"

                     "\nResult:\n"
                     "\"diviaddress\"  (string) A divi address associated with the keys.\n"

                     "\nExamples:\n"
                     "\nAdd a multisig address from 2 addresses\n" +
                HelpExampleCli("addmultisigaddress", "2 \"[\\\"Xt4qk9uKvQYAonVGSZNXqxeDmtjaEWgfrs\\\",\\\"XoSoWQkpgLpppPoyyzbUFh1fq2RBvW6UK1\\\"]\"") +
                "\nAs json rpc call\n" + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"Xt4qk9uKvQYAonVGSZNXqxeDmtjaEWgfrs\\\",\\\"XoSoWQkpgLpppPoyyzbUFh1fq2RBvW6UK1\\\"]\"");
        throw runtime_error(msg);
    }

    string strAccount;
    if (params.size() > 2)
        strAccount = AccountFromValue(params[2]);

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    pwalletMain->AddCScript(inner);
    if(::IsMine(*pwalletMain,inner)!= isminetype::ISMINE_SPENDABLE) pwalletMain->AddWatchOnly(inner);

    pwalletMain->SetAddressLabel(innerID, strAccount);
    return CBitcoinAddress(innerID).ToString();
}


struct tallyitem {
    CAmount nAmount;
    int nConf;
    int nBCConf;
    std::vector<uint256> txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
        nBCConf = std::numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

Value ListReceived(const Array& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    if (params.size() > 2)
        if (params[2].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();

    // Tally
    std::map<CBitcoinAddress, tallyitem> mapTally;
    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = pwalletMain->getConfirmationCalculator();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        const CWalletTx& wtx = *(*it);

        if (wtx.IsCoinBase() || !IsFinalTx(wtx, chain))
            continue;

        int nDepth = confsCalculator.GetNumberOfBlockConfirmations(wtx);
        int nBCDepth = nDepth;
        if (nDepth < nMinDepth)
            continue;

        BOOST_FOREACH (const CTxOut& txout, wtx.vout) {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            isminetype mine = pwalletMain->isMine(address);
            if (!filter.hasRequested(mine))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = min(item.nConf, nDepth);
            item.nBCConf = min(item.nBCConf, nBCDepth);
            item.txids.push_back(wtx.GetHash());
            if (mine == isminetype::ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    Array ret;
    map<string, tallyitem> mapAccountTally;
    BOOST_FOREACH (const PAIRTYPE(CBitcoinAddress, AddressLabel) & item, pwalletMain->GetAddressBookManager().GetAddressBook()) {
        const CBitcoinAddress& address = item.first;
        const string& strAccount = item.second.name;
        map<CBitcoinAddress, tallyitem>::iterator it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        int nBCConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end()) {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            nBCConf = (*it).second.nBCConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (fByAccounts) {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = min(item.nConf, nConf);
            item.nBCConf = min(item.nBCConf, nBCConf);
            item.fIsWatchonly = fIsWatchonly;
        } else {
            Object obj;
            if (fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("address", address.ToString()));
            obj.push_back(Pair("account", strAccount));
            obj.push_back(Pair("amount", ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            obj.push_back(Pair("bcconfirmations", (nBCConf == std::numeric_limits<int>::max() ? 0 : nBCConf)));
            Array transactions;
            if (it != mapTally.end()) {
                BOOST_FOREACH (const uint256& item, (*it).second.txids) {
                    transactions.push_back(item.GetHex());
                }
            }
            obj.push_back(Pair("txids", transactions));
            ret.push_back(obj);
        }
    }

    if (fByAccounts) {
        for (map<string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it) {
            CAmount nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            int nBCConf = (*it).second.nBCConf;
            Object obj;
            if ((*it).second.fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("account", (*it).first));
            obj.push_back(Pair("amount", ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            obj.push_back(Pair("bcconfirmations", (nBCConf == std::numeric_limits<int>::max() ? 0 : nBCConf)));
            ret.push_back(obj);
        }
    }

    return ret;
}

Value listreceivedbyaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
                "listreceivedbyaddress ( minconf includeempty includeWatchonly)\n"
                "\nList balances by receiving address.\n"
                "\nArguments:\n"
                "1. minconf       (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
                "2. includeempty  (numeric, optional, default=false) Whether to include addresses that haven't received any payments.\n"
                "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"involvesWatchonly\" : \"true\",    (bool) Only returned if imported addresses were involved in transaction\n"
                "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
                "    \"account\" : \"accountname\",       (string) The account of the receiving address. The default account is \"\".\n"
                "    \"amount\" : x.xxx,                  (numeric) The total amount in DIVI received by the address\n"
                "    \"confirmations\" : n                (numeric) The number of confirmations of the most recent transaction included\n"
                "    \"bcconfirmations\" : n              (numeric) The number of blockchain confirmations of the most recent transaction included\n"
                "  }\n"
                "  ,...\n"
                "]\n"

                "\nExamples:\n" +
                HelpExampleCli("listreceivedbyaddress", "") + HelpExampleCli("listreceivedbyaddress", "6 true") + HelpExampleRpc("listreceivedbyaddress", "6, true, true"));

    return ListReceived(params, false);
}

Value listreceivedbyaccount(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 3)
        throw runtime_error(
                "listreceivedbyaccount ( minconf includeempty includeWatchonly)\n"
                "\nList balances by account.\n"
                "\nArguments:\n"
                "1. minconf      (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
                "2. includeempty (boolean, optional, default=false) Whether to include accounts that haven't received any payments.\n"
                "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"involvesWatchonly\" : \"true\",    (bool) Only returned if imported addresses were involved in transaction\n"
                "    \"account\" : \"accountname\",  (string) The account name of the receiving account\n"
                "    \"amount\" : x.xxx,             (numeric) The total amount received by addresses with this account\n"
                "    \"confirmations\" : n           (numeric) The number of confirmations of the most recent transaction included\n"
                "    \"bcconfirmations\" : n         (numeric) The number of blockchain confirmations of the most recent transaction included\n"
                "  }\n"
                "  ,...\n"
                "]\n"

                "\nExamples:\n" +
                HelpExampleCli("listreceivedbyaccount", "") + HelpExampleCli("listreceivedbyaccount", "6 true") + HelpExampleRpc("listreceivedbyaccount", "6, true, true"));

    return ListReceived(params, true);
}

static void MaybePushAddress(Object& entry, const CTxDestination& dest)
{
    CBitcoinAddress addr;
    if (addr.Set(dest))
        entry.push_back(Pair("address", addr.ToString()));
}

static std::string GetAccountAddressName(const CWallet& wallet, const CTxDestination &dest)
{
    const AddressBook& addressBook = wallet.GetAddressBookManager().GetAddressBook();
    AddressBook::const_iterator mi = addressBook.find(dest);
    if (mi != addressBook.end() && !(*mi).second.name.empty())
    {
        return (*mi).second.name;
    }

    return std::string();
}

static int ComputeBlockHeightOfFirstConfirmation(const uint256 blockHash)
{
    LOCK(cs_main);
    const ChainstateManager::Reference chainstate;
    const auto& blockMap = chainstate->GetBlockMap();
    const auto it = blockMap.find(blockHash);
    return (it==blockMap.end() || it->second==nullptr)? 0 : it->second->nHeight;
}

static std::string ParseScriptAsAddressString(const CScript& scriptPubKey)
{
    CTxDestination parsedAddress;
    if(ExtractDestination(scriptPubKey, parsedAddress))
    {
        return CBitcoinAddress(parsedAddress).ToString();
    }
    else if(IsStakingVaultScript(scriptPubKey))
    {
        return std::string("Staking Vault");
    }
    else
    {
        return "Unknown address";
    }
}
void ParseTransactionDetails(const CWallet& wallet, const CWalletTx& wtx, const string& strAccount, int nMinDepth, bool fLong, Array& ret, const UtxoOwnershipFilter& filter)
{
    const SuperblockSubsidyContainer superblockSubsidies(Params(), GetSporkManager());
    const I_SuperblockHeightValidator& heightValidator = superblockSubsidies.superblockHeightValidator();
    const I_BlockSubsidyProvider& blockSubsidies = superblockSubsidies.blockSubsidiesProvider();

    string strSentAccount = wtx.strFromAccount;
    const std::string allAccounts = "*";
    bool fAllAccounts = (strAccount.compare(allAccounts) == 0);

    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = wallet.getConfirmationCalculator();
    const bool isCoinstake = wtx.IsCoinStake();
    const CAmount nDebit = wallet.getDebit(wtx,isminetype::ISMINE_SPENDABLE);

    if (isCoinstake)
    {
        const int blockHeight = ComputeBlockHeightOfFirstConfirmation(wtx.hashBlock);
        const bool isConfirmedBlock = blockHeight>0;
        const bool isLotteryPayment = isConfirmedBlock? heightValidator.IsValidLotteryBlockHeight(blockHeight): false;
        const CBlockRewards rewards = blockSubsidies.GetBlockSubsidity(blockHeight);
        bool parsingAmbiguityDetected = false;

        const isminetype stakerAddressOwnership = wallet.isMine(wtx.vout[1]);
        const bool stakerAddressIsSpendableByMe = stakerAddressOwnership != isminetype::ISMINE_NO;
        const CScript stakerScript = wtx.vout[1].scriptPubKey;
        if (stakerAddressIsSpendableByMe)
        {
            if(!fAllAccounts && strAccount != wtx.strFromAccount)
                return;

            CAmount stakerRewardTotal = 0;
            for (unsigned int i = 1; i < wtx.vout.size(); i++)
            {
                if(wtx.vout[i].scriptPubKey == stakerScript)
                {
                    stakerRewardTotal+= std::max(wtx.vout[i].nValue,CAmount(0));
                }

                if(!parsingAmbiguityDetected && stakerRewardTotal - nDebit > rewards.nStakeReward)
                {
                    parsingAmbiguityDetected = true;
                }
            }
            stakerRewardTotal -= nDebit;

            Object entry;
            //stake reward
            const std::string addressParsing = ParseScriptAsAddressString(stakerScript);
            entry.push_back(Pair("involvesWatchonly", stakerAddressOwnership == isminetype::ISMINE_WATCH_ONLY));
            entry.push_back(Pair("address", addressParsing));
            entry.push_back(Pair("amount", ValueFromAmount(stakerRewardTotal)));
            entry.push_back(Pair("vout", 1));
            entry.push_back(Pair("category", parsingAmbiguityDetected?"stake_reward":"stake_reward+"));
            entry.push_back(Pair("account", wtx.strFromAccount));

            if (fLong) WalletTxToJSON(wallet, wtx, entry);
            ret.push_back(entry);
        }
        if(isConfirmedBlock)
        {
            //if the address is not yours then it means you have a tx sent to you in someone elses coinstake tx
            for (unsigned int i = 1; i < wtx.vout.size(); i++)
            {
                const CScript& scriptPubKey = wtx.vout[i].scriptPubKey;
                isminetype mine = wallet.isMine(wtx.vout[i]);
                if (mine != isminetype::ISMINE_NO && scriptPubKey != stakerScript)
                {
                    CTxDestination outDestination;
                    ExtractDestination(scriptPubKey, outDestination);
                    auto strAccountForAddress = GetAccountAddressName(wallet, outDestination);
                    if(!fAllAccounts && strAccount != strAccountForAddress) continue;
                    const std::string addressParsing = ParseScriptAsAddressString(scriptPubKey);
                    Object entry;
                    entry.push_back(Pair("involvesWatchonly", mine == isminetype::ISMINE_WATCH_ONLY));
                    entry.push_back(Pair("address",  addressParsing ));
                    entry.push_back(Pair("amount", ValueFromAmount(wtx.vout[i].nValue)));
                    entry.push_back(Pair("vout", static_cast<int>(i)));
                    entry.push_back(Pair("category", isLotteryPayment ? "lottery" : "mn_reward"));
                    entry.push_back(Pair("account", strAccountForAddress));

                    if (fLong)
                        WalletTxToJSON(wallet,wtx, entry);

                    ret.push_back(entry);
                }
            }
        }
    }
    else {
        bool involvesWatchonly = false;
        bool fAllForMe = true;
        bool fAllFromMe = wallet.AllInputsAreMine(wtx);

        bool fMatchesReceiveAccount = false;
        typedef std::string AddressParsing;
        std::vector<std::pair<AddressParsing, std::string>> sendAddresses;
        for (const CTxOut& txout : wtx.vout) {
            CTxDestination dest;
            ExtractDestination(txout.scriptPubKey, dest);
            fAllForMe &= static_cast<bool>(wallet.isMine(txout) == isminetype::ISMINE_SPENDABLE);

            std::string account;
            const AddressBook& addressBook = pwalletMain->GetAddressBookManager().GetAddressBook();
            if (addressBook.count(dest)) {
                account = addressBook.find(dest)->second.name;
            }
            const AddressParsing addressParsing = ParseScriptAsAddressString(txout.scriptPubKey);
            sendAddresses.emplace_back(addressParsing, account);
            fMatchesReceiveAccount |= fAllAccounts || (account == strAccount);
        }

        if(fAllForMe && fMatchesReceiveAccount && fAllFromMe)
        {
            Object entry;
            entry.push_back(Pair("category", "move"));
            auto nFee = wallet.getDebit(wtx,isminetype::ISMINE_SPENDABLE) - wallet.getCredit(wtx,isminetype::ISMINE_SPENDABLE);
            entry.push_back(Pair("amount", ValueFromAmount( wallet.getDebit(wtx,isminetype::ISMINE_SPENDABLE) - wallet.GetChange(wtx) - nFee)));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));

            Array addresses;
            for(auto &&sendingAddress : sendAddresses) {
                Object obj;
                obj.push_back(Pair("address", sendingAddress.first));
                obj.push_back(Pair("account", sendingAddress.second));
                addresses.push_back(obj);
            }

            entry.push_back(Pair("addresses", addresses));

            if (fLong)
                WalletTxToJSON(wallet,wtx, entry);
            ret.push_back(entry);
        }
        else
        {
            WalletOutputEntryParsing parsedEntry = GetAmounts(wallet,wtx, filter);

            // Sent
            if ((!parsedEntry.listSent.empty() || parsedEntry.nFee != 0) && (fAllAccounts || strAccount == strSentAccount)) {
                BOOST_FOREACH (const COutputEntry& s, parsedEntry.listSent) {
                    Object entry;
                    if (involvesWatchonly || (wallet.isMine(s.destination) == isminetype::ISMINE_WATCH_ONLY))
                        entry.push_back(Pair("involvesWatchonly", true));
                    entry.push_back(Pair("account", strSentAccount));
                    MaybePushAddress(entry, s.destination);
                    std::map<std::string, std::string>::const_iterator it = wtx.mapValue.find("DS");
                    entry.push_back(Pair("category", (it != wtx.mapValue.end() && it->second == "1") ? "darksent" : "send"));
                    entry.push_back(Pair("amount", ValueFromAmount(-s.amount)));
                    entry.push_back(Pair("vout", s.vout));
                    entry.push_back(Pair("fee", ValueFromAmount(-parsedEntry.nFee)));
                    if (fLong)
                        WalletTxToJSON(wallet, wtx, entry);
                    ret.push_back(entry);
                }
            }

            // Received
            if (parsedEntry.listReceived.size() > 0 && confsCalculator.GetNumberOfBlockConfirmations(wtx) >= nMinDepth) {
                BOOST_FOREACH (const COutputEntry& r, parsedEntry.listReceived) {
                    string account;
                    const AddressBook& addressBook = pwalletMain->GetAddressBookManager().GetAddressBook();
                    if (addressBook.count(r.destination))
                        account = addressBook.find(r.destination)->second.name;
                    if (fAllAccounts || (account == strAccount)) {
                        Object entry;
                        if (involvesWatchonly || (wallet.isMine(r.destination) == isminetype::ISMINE_WATCH_ONLY))
                            entry.push_back(Pair("involvesWatchonly", true));
                        entry.push_back(Pair("account", account));
                        MaybePushAddress(entry, r.destination);
                        if (wtx.IsCoinBase()) {
                            if (confsCalculator.GetNumberOfBlockConfirmations(wtx) < 1)
                                entry.push_back(Pair("category", "orphan"));
                            else if (confsCalculator.GetBlocksToMaturity(wtx) > 0)
                                entry.push_back(Pair("category", "immature"));
                            else
                                entry.push_back(Pair("category", "generate"));
                        } else {
                            entry.push_back(Pair("category", "receive"));
                        }
                        entry.push_back(Pair("amount", ValueFromAmount(r.amount)));
                        entry.push_back(Pair("vout", r.vout));
                        if (fLong)
                            WalletTxToJSON(wallet, wtx, entry);
                        ret.push_back(entry);
                    }
                }
            }
        }
    }
}

Value listtransactions(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 4)
        throw runtime_error(
                "listtransactions ( \"account\" count from includeWatchonly)\n"
                "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.\n"
                "\nArguments:\n"
                "1. \"account\"    (string, optional) The account name. If not included, it will list all transactions for all accounts.\n"
                "                                     If \"\" is set, it will list transactions for the default account.\n"
                "2. count          (numeric, optional, default=10) The number of transactions to return\n"
                "3. from           (numeric, optional, default=0) The number of transactions to skip\n"
                "4. includeWatchonly (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')\n"
                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"account\":\"accountname\",       (string) The account name associated with the transaction. \n"
                "                                                It will be \"\" for the default account.\n"
                "    \"address\":\"diviaddress\",    (string) The divi address of the transaction. Not present for \n"
                "                                                move transactions (category = move).\n"
                "    \"category\":\"send|receive|move|mn_reward|stake_reward\", (string) The transaction category. 'move' is a local (off blockchain)\n"
                "                                                transaction between accounts, and not associated with an address,\n"
                "                                                transaction id or block. 'send' and 'receive' transactions are \n"
                "                                                associated with an address, transaction id and block details\n"
                "    \"amount\": x.xxx,          (numeric) The amount in DIVI. This is negative for the 'send' category, and for the\n"
                "                                         'move' category for moves outbound. It is positive for the 'receive' category,\n"
                "                                         and for the 'move' category for inbound funds.\n"
                "    \"vout\" : n,               (numeric) the vout value\n"
                "    \"fee\": x.xxx,             (numeric) The amount of the fee in DIVI. This is negative and only available for the \n"
                "                                         'send' category of transactions.\n"
                "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and \n"
                "                                         'receive' category of transactions.\n"
                "    \"bcconfirmations\": n,     (numeric) The number of blockchain confirmations for the transaction. Available for 'send'\n"
                "                                          and 'receive' category of transactions.\n"
                "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for 'send' and 'receive'\n"
                "                                          category of transactions.\n"
                "    \"blockindex\": n,          (numeric) The block index containing the transaction. Available for 'send' and 'receive'\n"
                "                                          category of transactions.\n"
                "    \"txid\": \"transactionid\", (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
                "    \"baretxid\" : \"baretxid\", (string) The bare txid (without signatures)\n"
                "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
                "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available \n"
                "                                          for 'send' and 'receive' category of transactions.\n"
                "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
                "    \"otheraccount\": \"accountname\",  (string) For the 'move' category of transactions, the account the funds came \n"
                "                                          from (for receiving funds, positive amounts), or went to (for sending funds,\n"
                "                                          negative amounts).\n"
                "  }\n"
                "]\n"

                "\nExamples:\n"
                "\nList the most recent 10 transactions in the systems\n" +
                HelpExampleCli("listtransactions", "") +
                "\nList the most recent 10 transactions for the tabby account\n" + HelpExampleCli("listtransactions", "\"tabby\"") +
                "\nList transactions 100 to 120 from the tabby account\n" + HelpExampleCli("listtransactions", "\"tabby\" 20 100") +
                "\nAs a json rpc call\n" + HelpExampleRpc("listtransactions", "\"tabby\", 20, 100"));

    string strAccount = "*";
    if (params.size() > 0)
        strAccount = params[0].get_str();
    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    if (params.size() > 3)
        if (params[3].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    Array ret;

    CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems();

    // iterate backwards until we have nCount items to return:
    for (CWallet::TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
        const CWalletTx* const pwtx = (*it).second;
        ParseTransactionDetails(*pwalletMain, *pwtx, strAccount, 0, true, ret, filter);
        if ((int)ret.size() >= (nCount + nFrom)) break;
    }
    // ret is newest to oldest

    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;
    Array::iterator first = ret.begin();
    std::advance(first, nFrom);
    Array::iterator last = ret.begin();
    std::advance(last, nFrom + nCount);

    if (last != ret.end()) ret.erase(last, ret.end());
    if (first != ret.begin()) ret.erase(ret.begin(), first);

    std::reverse(ret.begin(), ret.end()); // Return oldest to newest

    return ret;
}

Value listaccounts(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
                "listaccounts ( minconf includeWatchonly)\n"
                "\nReturns Object that has account names as keys, account balances as values.\n"
                "\nArguments:\n"
                "1. minconf          (numeric, optional, default=1) Only include transactions with at least this many confirmations\n"
                "2. includeWatchonly (bool, optional, default=false) Include balances in watchonly addresses (see 'importaddress')\n"
                "\nResult:\n"
                "{                      (json object where keys are account names, and values are numeric balances\n"
                "  \"account\": x.xxx,  (numeric) The property name is the account name, and the value is the total balance for the account.\n"
                "  ...\n"
                "}\n"
                "\nExamples:\n"
                "\nList account balances where there at least 1 confirmation\n" +
                HelpExampleCli("listaccounts", "") +
                "\nList account balances including zero confirmation transactions\n" + HelpExampleCli("listaccounts", "0") +
                "\nList account balances for 6 or more confirmations\n" + HelpExampleCli("listaccounts", "6") +
                "\nAs json rpc call\n" + HelpExampleRpc("listaccounts", "6"));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    if (params.size() > 1)
        if (params[1].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    map<string, CAmount> mapAccountBalances;
    BOOST_FOREACH (const PAIRTYPE(CTxDestination, AddressLabel) & entry, pwalletMain->GetAddressBookManager().GetAddressBook()) {
        if (filter.hasRequested(pwalletMain->isMine(entry.first))) // This address belongs to me
            mapAccountBalances[entry.second.name] = 0;
    }

    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    const I_MerkleTxConfirmationNumberCalculator& confsCalculator = pwalletMain->getConfirmationCalculator();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        const CWalletTx& wtx = *(*it);
        string strSentAccount = wtx.strFromAccount;
        int nDepth = confsCalculator.GetNumberOfBlockConfirmations(wtx);
        if (confsCalculator.GetBlocksToMaturity(wtx) > 0 || nDepth < 0)
            continue;
        WalletOutputEntryParsing parsedEntry = GetAmounts(*pwalletMain,wtx, filter);
        mapAccountBalances[strSentAccount] -= parsedEntry.nFee;
        BOOST_FOREACH (const COutputEntry& s, parsedEntry.listSent)
                mapAccountBalances[strSentAccount] -= s.amount;
        if (nDepth >= nMinDepth)
        {
            const AddressBook& addressBook = pwalletMain->GetAddressBookManager().GetAddressBook();
            BOOST_FOREACH (const COutputEntry& r, parsedEntry.listReceived)
            {
                if (addressBook.count(r.destination))
                    mapAccountBalances[addressBook.find(r.destination)->second.name] += r.amount;
                else
                    mapAccountBalances[""] += r.amount;
            }
        }
    }

    Object ret;
    BOOST_FOREACH (const PAIRTYPE(string, CAmount) & accountBalance, mapAccountBalances) {
        ret.push_back(Pair(accountBalance.first, ValueFromAmount(accountBalance.second)));
    }
    return ret;
}

Value listsinceblock(const Array& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
                "listsinceblock ( \"blockhash\" target-confirmations includeWatchonly)\n"
                "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted\n"
                "\nArguments:\n"
                "1. \"blockhash\"   (string, optional) The block hash to list transactions since\n"
                "2. target-confirmations:    (numeric, optional) The confirmations required, must be 1 or more\n"
                "3. includeWatchonly:        (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')"
                "\nResult:\n"
                "{\n"
                "  \"transactions\": [\n"
                "    \"account\":\"accountname\",       (string) The account name associated with the transaction. Will be \"\" for the default account.\n"
                "    \"address\":\"diviaddress\",    (string) The divi address of the transaction. Not present for move transactions (category = move).\n"
                "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.\n"
                "    \"amount\": x.xxx,          (numeric) The amount in DIVI. This is negative for the 'send' category, and for the 'move' category for moves \n"
                "                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.\n"
                "    \"vout\" : n,               (numeric) the vout value\n"
                "    \"fee\": x.xxx,             (numeric) The amount of the fee in DIVI. This is negative and only available for the 'send' category of transactions.\n"
                "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
                "    \"bcconfirmations\" : n,    (numeric) The number of blockchain confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
                "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
                "    \"blockindex\": n,          (numeric) The block index containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
                "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
                "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
                "    \"baretxid\" : \"baretxid\",  (string) The bare txid (without signatures)\n"
                "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
                "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.\n"
                "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
                "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
                "  ],\n"
                "  \"lastblock\": \"lastblockhash\"     (string) The hash of the last block\n"
                "}\n"
                "\nExamples:\n" +
                HelpExampleCli("listsinceblock", "") + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6") + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6"));

    CBlockIndex* pindex = NULL;
    int target_confirms = 1;
    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);

    const ChainstateManager::Reference chainstate;
    const auto& chain = chainstate->ActiveChain();
    const auto& blockMap = chainstate->GetBlockMap();

    if (params.size() > 0) {
        uint256 blockId = 0;

        blockId.SetHex(params[0].get_str());
        const auto it = blockMap.find(blockId);
        if (it != blockMap.end())
            pindex = it->second;
    }

    if (params.size() > 1) {
        target_confirms = params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    if (params.size() > 2)
        if (params[2].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    int depth = pindex ? (1 + chain.Height() - pindex->nHeight) : -1;

    Array transactions;

    std::vector<const CWalletTx*> walletTransactions = pwalletMain->GetWalletTransactionReferences();
    for (std::vector<const CWalletTx*>::iterator it = walletTransactions.begin(); it != walletTransactions.end(); ++it)
    {
        CWalletTx tx = *(*it);

        if (depth == -1 || pwalletMain->getConfirmationCalculator().GetNumberOfBlockConfirmations(tx) < depth)
            ParseTransactionDetails(*pwalletMain, tx, "*", 0, true, transactions, filter);
    }

    const CBlockIndex* pblockLast = chain[chain.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : 0;

    Object ret;
    ret.push_back(Pair("transactions", transactions));
    ret.push_back(Pair("lastblock", lastblock.GetHex()));

    return ret;
}

Value gettransaction(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "gettransaction \"txid\" ( includeWatchonly )\n"
                "\nGet detailed information about in-wallet transaction <txid>\n"
                "\nArguments:\n"
                "1. \"txid\"    (string, required) The transaction id\n"
                "2. \"includeWatchonly\"    (bool, optional, default=false) Whether to include watchonly addresses in balance calculation and details[]\n"
                "\nResult:\n"
                "{\n"
                "  \"amount\" : x.xxx,        (numeric) The transaction amount in DIVI\n"
                "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
                "  \"bcconfirmations\" : n,   (numeric) The number of blockchain confirmations\n"
                "  \"blockhash\" : \"hash\",  (string) The block hash\n"
                "  \"blockindex\" : xx,       (numeric) The block index\n"
                "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"txid\" : \"transactionid\",   (string) The transaction id.\n"
                "  \"baretxid\" : \"baretxid\",    (string) The bare txid (without signatures)\n"
                "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
                "  \"details\" : [\n"
                "    {\n"
                "      \"account\" : \"accountname\",  (string) The account name involved in the transaction, can be \"\" for the default account.\n"
                "      \"address\" : \"diviaddress\",   (string) The divi address involved in the transaction\n"
                "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
                "      \"amount\" : x.xxx                  (numeric) The amount in DIVI\n"
                "      \"vout\" : n,                       (numeric) the vout value\n"
                "    }\n"
                "    ,...\n"
                "  ],\n"
                "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
                "}\n"

                "\nExamples:\n" +
                HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"") + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true") + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\""));

    uint256 hash;
    hash.SetHex(params[0].get_str());

    UtxoOwnershipFilter filter;
    filter.addOwnershipType(isminetype::ISMINE_SPENDABLE);
    if (params.size() > 1)
        if (params[1].get_bool())
            filter.addOwnershipType(isminetype::ISMINE_WATCH_ONLY);

    Object entry;
    const CWalletTx* txPtr = pwalletMain->GetWalletTx(hash);
    if (txPtr == nullptr)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = *txPtr;

    CAmount nCredit = pwalletMain->getCredit(wtx,filter);
    CAmount nDebit = pwalletMain->getDebit(wtx,filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee =  nDebit > 0 ? nDebit - wtx.GetValueOut(): 0;

    entry.push_back(Pair("amount", ValueFromAmount(nNet)));
    if (nDebit > 0) entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(*pwalletMain,wtx, entry);

    Array details;
    ParseTransactionDetails(*pwalletMain, wtx, "*", 0, false, details, filter);
    entry.push_back(Pair("details", details));

    string strHex = EncodeHexTx(static_cast<CTransaction>(wtx));
    entry.push_back(Pair("hex", strHex));

    return entry;
}


Value backupwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "backupwallet \"destination\"\n"
                "\nSafely copies wallet.dat to destination, which can be a directory or a path with filename.\n"
                "\nArguments:\n"
                "1. \"destination\"   (string) The destination directory or file\n"
                "\nExamples:\n" +
                HelpExampleCli("backupwallet", "\"backup.dat\"") + HelpExampleRpc("backupwallet", "\"backup.dat\""));

    string strDest = params[0].get_str();
    if (!pwalletMain->isBackedByFile() || !BackupWallet(settings,pwalletMain->dbFilename(), strDest))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

    return Value::null;
}


Value keypoolrefill(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
                "keypoolrefill ( newsize )\n"
                "\nFills the keypool." +
                HelpRequiringPassphrase() + "\n"
                                            "\nArguments\n"
                                            "1. newsize     (numeric, optional, default=100) The new keypool size\n"
                                            "\nExamples:\n" +
                HelpExampleCli("keypoolrefill", "") + HelpExampleRpc("keypoolrefill", ""));

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (params.size() > 0) {
        if (params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)params[0].get_int();
    }

    EnsureWalletIsUnlocked();
    pwalletMain->TopUpKeyPool(kpSize);

    if (pwalletMain->GetKeyPoolSize() < kpSize)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");

    return Value::null;
}

Value walletpassphrase(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() < 2 || params.size() > 3))
        throw runtime_error(
                "walletpassphrase \"passphrase\" timeout ( stakingOnly )\n"
                "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
                "This is needed prior to performing transactions related to private keys such as sending DIVs\n"
                "\nArguments:\n"
                "1. \"passphrase\"     (string, required) The wallet passphrase\n"
                "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
                "3. stakingOnly      (boolean, optional, default=flase) If is true sending functions are disabled."
                "\nNote:\n"
                "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
                "time that overrides the old one. A timeout of \"0\" unlocks until the wallet is closed.\n"
                "\nExamples:\n"
                "\nUnlock the wallet for 60 seconds\n" +
                HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
                "\nUnlock the wallet for 60 seconds but allow Obfuscation only\n" + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60 true") +
                "\nLock the wallet again (before 60 seconds)\n" + HelpExampleCli("walletlock", "") +
                "\nAs json rpc call\n" + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60"));

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    strWalletPass = params[0].get_str().c_str();

    bool stakingOnly = false;
    if (params.size() == 3)
        stakingOnly = params[2].get_bool();


    if(pwalletMain->IsUnlockedForStakingOnly() && stakingOnly)
    {
        throw JSONRPCError(RPC_WALLET_ALREADY_UNLOCKED, "Error: Wallet is already unlocked for staking.");
    }

    if (!pwalletMain->Unlock(strWalletPass, stakingOnly))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    pwalletMain->TopUpKeyPool();

    int64_t nSleepTime = params[1].get_int64();
    UnlockWalletBriefly(nSleepTime);
    return Value::null;
}


Value walletpassphrasechange(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
                "walletpassphrasechange \"oldpassphrase\" \"newpassphrase\"\n"
                "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
                "\nArguments:\n"
                "1. \"oldpassphrase\"      (string) The current passphrase\n"
                "2. \"newpassphrase\"      (string) The new passphrase\n"
                "\nExamples:\n" +
                HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"") + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\""));

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw runtime_error(
                "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
                "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    return Value::null;
}


Value walletlock(const Array& params, bool fHelp)
{
    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw runtime_error(
                "walletlock\n"
                "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
                "After calling this method, you will need to call walletpassphrase again\n"
                "before being able to call any methods which require the wallet to be unlocked.\n"
                "\nExamples:\n"
                "\nSet the passphrase for 2 minutes to perform a transaction\n" +
                HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
                "\nPerform a send (requires passphrase set)\n" + HelpExampleCli("sendtoaddress", "\"XwnLY9Tf7Zsef8gMGL2fhWA9ZmMjt4KPwg\" 1.0") +
                "\nClear the passphrase since we are done before 2 minutes is up\n" + HelpExampleCli("walletlock", "") +
                "\nAs json rpc call\n" + HelpExampleRpc("walletlock", ""));

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");

    LockWallet();

    return Value::null;
}

Value walletverify(const json_spirit::Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "walletverify\n"
                "\nChecks wallet integrity, if this returns true, you can be sure that all funds are accesible\n");

    if (fHelp)
        return true;

    EnsureWalletIsUnlocked();

    if(!pwalletMain->IsHDEnabled())
        throw runtime_error("HD wallet is disabled, checking integrity works only with HD wallets");

    if(!pwalletMain->VerifyHDKeys())
    {
        return false;
    }

    return true;
}

Value encryptwallet(const Array& params, bool fHelp)
{
    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw runtime_error(
                "encryptwallet \"passphrase\"\n"
                "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
                "After this, any calls that interact with private keys such as sending or signing \n"
                "will require the passphrase to be set prior the making these calls.\n"
                "Use the walletpassphrase call for this, and then walletlock call.\n"
                "If the wallet is already encrypted, use the walletpassphrasechange call.\n"
                "Note that this will shutdown the server.\n"
                "\nArguments:\n"
                "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.\n"
                "\nExamples:\n"
                "\nEncrypt you wallet\n" +
                HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
                "\nNow set the passphrase to use the wallet, such as for signing or sending DIVs\n" + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
                "\nNow we can so something like sign\n" + HelpExampleCli("signmessage", "\"diviaddress\" \"test message\"") +
                "\nNow lock the wallet again by removing the passphrase\n" + HelpExampleCli("walletlock", "") +
                "\nAs a json rpc call\n" + HelpExampleRpc("encryptwallet", "\"my pass phrase\""));

    if (fHelp)
        return true;
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw runtime_error(
                "encryptwallet <passphrase>\n"
                "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return "wallet encrypted; divi server stopping, restart to run with encrypted wallet. The keypool has been flushed, you need to make a new backup.";
}

Value lockunspent(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
                "lockunspent unlock [{\"txid\":\"txid\",\"vout\":n},...]\n"
                "\nUpdates list of temporarily unspendable outputs.\n"
                "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
                "A locked transaction output will not be chosen by automatic coin selection, when spending DIVs.\n"
                "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
                "is always cleared (by virtue of process exit) when a node stops or fails.\n"
                "Also see the listunspent call\n"
                "\nArguments:\n"
                "1. unlock            (boolean, required) Whether to unlock (true) or lock (false) the specified transactions\n"
                "2. \"transactions\"  (string, required) A json array of objects. Each object the txid (string) vout (numeric)\n"
                "     [           (json array of json objects)\n"
                "       {\n"
                "         \"txid\":\"id\",    (string) The transaction id\n"
                "         \"vout\": n         (numeric) The output number\n"
                "       }\n"
                "       ,...\n"
                "     ]\n"

                "\nResult:\n"
                "true|false    (boolean) Whether the command was successful or not\n"

                "\nExamples:\n"
                "\nList the unspent transactions\n" +
                HelpExampleCli("listunspent", "") +
                "\nLock an unspent transaction\n" + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nList the locked transactions\n" + HelpExampleCli("listlockunspent", "") +
                "\nUnlock the transaction again\n" + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nAs a json rpc call\n" + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\""));

    if (params.size() == 1)
        RPCTypeCheck(params, list_of(bool_type));
    else
        RPCTypeCheck(params, list_of(bool_type)(array_type));

    const bool fUnlock = params[0].get_bool();

    LOCK(pwalletMain->cs_wallet);

    if (params.size() == 1) {
        if (fUnlock)
            pwalletMain->UnlockAllCoins();
        return true;
    }

    Array outputs = params[1].get_array();
    BOOST_FOREACH (Value& output, outputs) {
        if (output.type() != obj_type)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const Object& o = output.get_obj();

        RPCTypeCheck(o, map_list_of("txid", str_type)("vout", int_type));

        string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256(txid), nOutput);

        if (fUnlock)
            pwalletMain->UnlockCoin(outpt);
        else
            pwalletMain->LockCoin(outpt);
    }

    return true;
}

Value listlockunspent(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
                "listlockunspent\n"
                "\nReturns list of temporarily unspendable outputs.\n"
                "See the lockunspent call to lock and unlock transactions for spending.\n"
                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"txid\" : \"transactionid\",     (string) The transaction id locked\n"
                "    \"vout\" : n                      (numeric) The vout value\n"
                "  }\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n"
                "\nList the unspent transactions\n" +
                HelpExampleCli("listunspent", "") +
                "\nLock an unspent transaction\n" + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nList the locked transactions\n" + HelpExampleCli("listlockunspent", "") +
                "\nUnlock the transaction again\n" + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
                "\nAs a json rpc call\n" + HelpExampleRpc("listlockunspent", ""));

    std::vector<COutPoint> vOutpts;
    pwalletMain->ListLockedCoins(vOutpts);

    Array ret;

    BOOST_FOREACH (COutPoint& outpt, vOutpts) {
        Object o;

        o.push_back(Pair("txid", outpt.hash.GetHex()));
        o.push_back(Pair("vout", (int)outpt.n));
        ret.push_back(o);
    }

    return ret;
}

static std::string DescribeEncryptionStatus(CWallet *wallet)
{
    if(wallet->IsCrypted())
    {
        if(wallet->IsUnlockedForStakingOnly()) {
            return "unlocked-for-staking";
        }
        else if(wallet->IsLocked()) {
            return "locked";
        }
        else {
            return "unlocked";
        }
    }

    return "unencrypted";
}

Value getwalletinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
                "getwalletinfo\n"
                "Returns an object containing various wallet state info.\n"
                "\nResult:\n"
                "{\n"
                "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
                "  \"balance\": xxxxxxx,         (numeric) the total DIVI balance of the wallet\n"
                "  \"txcount\": xxxxxxx,         (numeric) the total number of transactions in the wallet\n"
                "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
                "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
                "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
                "  \"encryption_status\": status (string) encryption status, possible values: unencrypted/unlocked/locked/locked-anonymization"
                "}\n"
                "\nExamples:\n" +
                HelpExampleCli("getwalletinfo", "") + HelpExampleRpc("getwalletinfo", ""));

    CHDChain hdChainCurrent;
    bool fHDEnabled = pwalletMain->GetHDChain(hdChainCurrent);
    Object obj;
    obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
    obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
    obj.push_back(Pair("unconfirmed_balance", ValueFromAmount(pwalletMain->GetUnconfirmedBalance())));
    obj.push_back(Pair("immature_balance",    ValueFromAmount(pwalletMain->GetImmatureBalance())));
    obj.push_back(Pair("txcount", (int)pwalletMain->GetWalletTransactionReferences().size()));
    obj.push_back(Pair("keypoolsize", (int)pwalletMain->GetKeyPoolSize()));
    if (pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", TimeTillWalletLock() ));

    obj.push_back(Pair("encryption_status", DescribeEncryptionStatus(pwalletMain)));

    if (fHDEnabled) {
        obj.push_back(Pair("hdchainid", hdChainCurrent.GetID().GetHex()));
        obj.push_back(Pair("hdaccountcount", (int64_t)hdChainCurrent.CountAccounts()));
        Array accounts;
        for (size_t i = 0; i < hdChainCurrent.CountAccounts(); ++i)
        {
            CHDAccount acc;
            Object account;
            account.push_back(Pair("hdaccountindex", (int64_t)i));
            if(hdChainCurrent.GetAccount(i, acc)) {
                account.push_back(Pair("hdexternalkeyindex", (int64_t)acc.nExternalChainCounter));
                account.push_back(Pair("hdinternalkeyindex", (int64_t)acc.nInternalChainCounter));
            } else {
                account.push_back(Pair("error", strprintf("account %d is missing", i)));
            }
            accounts.push_back(account);
        }
        obj.push_back(Pair("hdaccounts", accounts));
    }
    return obj;
}
