// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include <DataDirectory.h>
#include "dbenv.h"
#include "hash.h"
#include <Logging.h>
#include "protocol.h"
#include "util.h"
#include "utilstrencodings.h"

#include <stdint.h>

#ifndef WIN32
#include <sys/stat.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/version.hpp>

#include <openssl/rand.h>
#include "Settings.h"

//
// CDB
//
void CDB::Init()
{
    int ret = 0;
    if (strFile.empty())
        return;

    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK(bitdb_.cs_db);
        if (!bitdb_.Open(GetDataDir()))
            throw std::runtime_error("CDB : Failed to open database environment.");

        assert(!strFile.empty());
        ++bitdb_.mapFileUseCount[strFile];
        pdb = bitdb_.mapDb[strFile];
        if (pdb == NULL) {
            pdb = new Db(&bitdb_.dbenv, 0);

            bool fMockDb = bitdb_.IsMock();
            if (fMockDb) {
                DbMpoolFile* mpf = pdb->get_mpf();
                ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
                if (ret != 0)
                {
                    delete pdb;
                    throw std::runtime_error(strprintf("CDB : Failed to configure for no temp file backing for database %s", strFile));
                }
            }

            ret = pdb->open(NULL,                   // Txn pointer
                fMockDb ? NULL : strFile.c_str(),   // Filename
                fMockDb ? strFile.c_str() : "main", // Logical db name
                DB_BTREE,                           // Database type
                nFlags,                             // Flags
                0);

            if (ret != 0) {
                delete pdb;
                pdb = NULL;
                const std::string databaseFilename = strFile;
                --bitdb_.mapFileUseCount[strFile];
                strFile = "";
                throw std::runtime_error(strprintf("CDB : Error %d, can't open database %s", ret, databaseFilename));
            }

            if (fCreate && !Exists(std::string("version"))) {
                bool fTmp = fReadOnly;
                fReadOnly = false;
                WriteVersion(CLIENT_VERSION);
                fReadOnly = fTmp;
            }

            bitdb_.mapDb[strFile] = pdb;
        }
    }
    isOpen = true;
}

void CDB::Open(const Settings& settings, const char* pszMode)
{
    assert(!isOpen);
    pdb = NULL;
    activeTxn = NULL;
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    fCreate = strchr(pszMode, 'c') != NULL;
    dbLogMinutes = fReadOnly? 1u:0u;
    dbLogSize = fReadOnly? (settings.GetArg("-dblogsize", 100) * 1024u) : 0u;
    Init();
}

CDB::CDB(
    CDBEnv& bitdb,
    const std::string& strFilename
    ) : bitdb_(bitdb)
    , pdb(NULL)
    , strFile(strFilename)
    , activeTxn(NULL)
    , fReadOnly(false)
    , fCreate(false)
    , dbLogMinutes(0u)
    , dbLogSize(0u)
    , isOpen(false)
{
}

void CDB::Flush()
{
    if (activeTxn)
        return;

    bitdb_.dbenv.txn_checkpoint(dbLogSize, dbLogMinutes, 0);
}

void CDB::Close()
{
    if (!pdb)
        return;
    if (activeTxn)
        activeTxn->abort();
    activeTxn = NULL;
    pdb = NULL;

    Flush();

    {
        LOCK(bitdb_.cs_db);
        --bitdb_.mapFileUseCount[strFile];
    }
    isOpen = false;
}

bool CDB::Rewrite(const Settings& settings, CDBEnv& bitdb, const std::string& strFile, const char* pszSkip)
{
    while (true) {
        {
            LOCK(bitdb.cs_db);
            if (!bitdb.mapFileUseCount.count(strFile) || bitdb.mapFileUseCount[strFile] == 0) {
                // Flush log data to the dat file
                bitdb.CloseDb(strFile);
                bitdb.CheckpointLSN(strFile);
                bitdb.mapFileUseCount.erase(strFile);

                bool fSuccess = true;
                LogPrintf("CDB::Rewrite : Rewriting %s...\n", strFile);
                std::string strFileRes = strFile + ".rewrite";
                { // surround usage of db with extra {}
                    CDB db(bitdb, strFile.c_str());
                    db.Open(settings,"r");
                    Db* pdbCopy = new Db(&bitdb.dbenv, 0);

                    int ret = pdbCopy->open(NULL, // Txn pointer
                        strFileRes.c_str(),       // Filename
                        "main",                   // Logical db name
                        DB_BTREE,                 // Database type
                        DB_CREATE,                // Flags
                        0);
                    if (ret > 0) {
                        LogPrintf("CDB::Rewrite : Can't create database file %s\n", strFileRes);
                        fSuccess = false;
                    }

                    Dbc* pcursor = db.GetCursor();
                    if (pcursor)
                        while (fSuccess) {
                            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
                            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
                            int ret = db.ReadAtCursor(pcursor, ssKey, ssValue, DB_NEXT);
                            if (ret == DB_NOTFOUND) {
                                pcursor->close();
                                break;
                            } else if (ret != 0) {
                                pcursor->close();
                                fSuccess = false;
                                break;
                            }
                            if (pszSkip &&
                                strncmp(&ssKey[0], pszSkip, std::min(ssKey.size(), strlen(pszSkip))) == 0)
                                continue;
                            if (strncmp(&ssKey[0], "\x07version", 8) == 0) {
                                // Update version:
                                ssValue.clear();
                                ssValue << CLIENT_VERSION;
                            }
                            Dbt datKey(&ssKey[0], ssKey.size());
                            Dbt datValue(&ssValue[0], ssValue.size());
                            int ret2 = pdbCopy->put(NULL, &datKey, &datValue, DB_NOOVERWRITE);
                            if (ret2 > 0)
                                fSuccess = false;
                        }
                    if (fSuccess) {
                        db.Close();
                        bitdb.CloseDb(strFile);
                        if (pdbCopy->close(0))
                            fSuccess = false;
                        delete pdbCopy;
                    }
                }
                if (fSuccess) {
                    Db dbA(&bitdb.dbenv, 0);
                    if (dbA.remove(strFile.c_str(), NULL, 0))
                        fSuccess = false;
                    Db dbB(&bitdb.dbenv, 0);
                    if (dbB.rename(strFileRes.c_str(), NULL, strFile.c_str(), 0))
                        fSuccess = false;
                }
                if (!fSuccess)
                    LogPrintf("CDB::Rewrite : Failed to rewrite database file %s\n", strFileRes);
                return fSuccess;
            }
        }
        MilliSleep(100);
    }
    return false;
}

bool CDB::TxnBegin()
{
    if (!pdb || activeTxn)
        return false;
    DbTxn* ptxn = bitdb_.TxnBegin();
    if (!ptxn)
        return false;
    activeTxn = ptxn;
    return true;
}