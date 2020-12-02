// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"
#include "walletdb.h"
#include <WalletTx.h>

#include <stdint.h>

#include <boost/foreach.hpp>
#include <boost/test/unit_test.hpp>

extern CWallet* pwalletMain;

BOOST_AUTO_TEST_SUITE(accounting_tests)

static void
GetResults(CWalletDB& walletdb, std::map<int64_t, CAccountingEntry>& results)
{
    std::list<CAccountingEntry> aes;

    results.clear();
    BOOST_CHECK(pwalletMain->ReorderTransactionsByTimestamp() == DB_LOAD_OK);
    walletdb.ListAccountCreditDebit("", aes);
    BOOST_FOREACH(CAccountingEntry& ae, aes)
    {
        results[ae.nOrderPos] = ae;
    }
}

static std::vector<std::string> accountComments;
void writeAccountEntry(CWalletDB& walletdb,CAccountingEntry& accountingEntry, int64_t timestamp)
{
    static std::string fromAccount = "";
    static unsigned accountEntryNumber = 0;
    static CAmount creditingValue = 1;

    std::string toAccount = std::string("receiving-account:")+std::to_string(accountEntryNumber);
    std::string comment = std::string("no-comment:")+std::to_string(accountEntryNumber);
    accountingEntry.strAccount= fromAccount;
    accountingEntry.nCreditDebit= creditingValue;
    accountingEntry.nTime = timestamp;
    accountingEntry.strOtherAccount = toAccount;
    accountingEntry.strComment =  comment;

    ++accountEntryNumber;
    walletdb.WriteAccountingEntry(accountingEntry);
    accountComments.push_back(comment);
}
void writeAccountEntry(CWalletDB& walletdb, int64_t timestamp)
{
    CAccountingEntry accountingEntry;
    writeAccountEntry(walletdb,accountingEntry,timestamp);
}
void writeAccountEntry(CWalletDB& walletdb, int64_t timestamp, int64_t orderPos)
{
    CAccountingEntry accountingEntry;
    accountingEntry.nOrderPos = orderPos;
    writeAccountEntry(walletdb,accountingEntry,timestamp);
}
CWalletTx* writeWalletEntry(CWallet* wallet,CWalletTx& wtx, int64_t timestamp)
{
    static unsigned walletTxIndex = 0;
    wtx.mapValue["comment"] = std::string("receving-tx:")+std::to_string(walletTxIndex++);
    wallet->AddToWallet(wtx);
    CWalletTx* walletTx = const_cast<CWalletTx*>(wallet->GetWalletTx(wtx.GetHash()));
    walletTx->nTimeReceived = (unsigned int)timestamp;
    return walletTx;
}
CWalletTx* writeWalletEntry(CWallet* wallet,CWalletTx& wtx, int64_t timestamp, int64_t orderPos)
{
    CWalletTx* walletTx = writeWalletEntry(wallet,wtx,timestamp);
    walletTx->nOrderPos = orderPos;
    return walletTx;
}

BOOST_AUTO_TEST_CASE(acc_orderupgrade)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    std::vector<CWalletTx*> vpwtx;
    CWalletTx wtx;
    CAccountingEntry ae;
    std::map<int64_t, CAccountingEntry> results;

    LOCK(pwalletMain->cs_wallet);

    writeAccountEntry(walletdb,1333333333);
    vpwtx.push_back( writeWalletEntry(pwalletMain,wtx,1333333335,-1) );
    writeAccountEntry(walletdb,1333333336);

    GetResults(walletdb, results);

    BOOST_CHECK(pwalletMain->GetNextTransactionIndexAvailable() == 3);
    BOOST_CHECK(2 == results.size());
    BOOST_CHECK(results[0].nTime == 1333333333);
    BOOST_CHECK(results[0].strComment == accountComments[0]);
    BOOST_CHECK(1 == vpwtx[0]->nOrderPos);
    BOOST_CHECK(results[2].nTime == 1333333336);
    BOOST_CHECK(results[2].strComment == accountComments[1]);

    writeAccountEntry(walletdb,1333333330,3);

    GetResults(walletdb, results);

    BOOST_CHECK(results.size() == 3);
    BOOST_CHECK(pwalletMain->GetNextTransactionIndexAvailable() == 4);
    BOOST_CHECK(results[0].nTime == 1333333333);
    BOOST_CHECK(1 == vpwtx[0]->nOrderPos);
    BOOST_CHECK(results[2].nTime == 1333333336);
    BOOST_CHECK(results[3].nTime == 1333333330);
    BOOST_CHECK(results[3].strComment == accountComments[2]);

    {
        CMutableTransaction tx(wtx);
        --tx.nLockTime;  // Just to change the hash :)
        *static_cast<CTransaction*>(&wtx) = CTransaction(tx);
    }
    vpwtx.push_back( writeWalletEntry(pwalletMain,wtx,1333333336) );

    {
        CMutableTransaction tx(wtx);
        --tx.nLockTime;  // Just to change the hash :)
        *static_cast<CTransaction*>(&wtx) = CTransaction(tx);
    }
    vpwtx.push_back( writeWalletEntry(pwalletMain,wtx,1333333329,-1) );

    GetResults(walletdb, results);

    BOOST_CHECK(results.size() == 3);
    BOOST_CHECK(pwalletMain->GetNextTransactionIndexAvailable() == 6);
    BOOST_CHECK(0 == vpwtx[2]->nOrderPos);
    BOOST_CHECK(results[1].nTime == 1333333333);
    BOOST_CHECK(2 == vpwtx[0]->nOrderPos);
    BOOST_CHECK(results[3].nTime == 1333333336);
    BOOST_CHECK(results[4].nTime == 1333333330);
    BOOST_CHECK(results[4].strComment == accountComments[2]);
    BOOST_CHECK(5 == vpwtx[1]->nOrderPos);

    writeAccountEntry(walletdb,1333333334,-1);

    GetResults(walletdb, results);

    BOOST_CHECK(results.size() == 4);
    BOOST_CHECK(pwalletMain->GetNextTransactionIndexAvailable() == 7);
    BOOST_CHECK(0 == vpwtx[2]->nOrderPos);
    BOOST_CHECK(results[1].nTime == 1333333333);
    BOOST_CHECK(2 == vpwtx[0]->nOrderPos);
    BOOST_CHECK(results[3].nTime == 1333333336);
    BOOST_CHECK(results[3].strComment == accountComments[1]);
    BOOST_CHECK(results[4].nTime == 1333333330);
    BOOST_CHECK(results[4].strComment ==  accountComments[2]);
    BOOST_CHECK(results[5].nTime == 1333333334);
    BOOST_CHECK(6 == vpwtx[1]->nOrderPos);
}

BOOST_AUTO_TEST_SUITE_END()
