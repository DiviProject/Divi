// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"

#include "addrman.h"
#include "hash.h"
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


unsigned int nWalletDBUpdated;


//
// CDB
//

CDBEnv CDB::bitdb;

CDB::CDB(const std::string& strFilename, const char* pszMode) : pdb(NULL), activeTxn(NULL)
{
    int ret;
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    if (strFilename.empty())
        return;

    bool fCreate = strchr(pszMode, 'c') != NULL;
    unsigned int nFlags = DB_THREAD;
    if (fCreate)
        nFlags |= DB_CREATE;

    {
        LOCK(CDB::bitdb.cs_db);
        if (!CDB::bitdb.Open(GetDataDir()))
            throw runtime_error("CDB : Failed to open database environment.");

        strFile = strFilename;
        ++CDB::bitdb.mapFileUseCount[strFile];
        pdb = CDB::bitdb.mapDb[strFile];
        if (pdb == NULL) {
            pdb = new Db(&CDB::bitdb.dbenv, 0);

            bool fMockDb = CDB::bitdb.IsMock();
            if (fMockDb) {
                DbMpoolFile* mpf = pdb->get_mpf();
                ret = mpf->set_flags(DB_MPOOL_NOFILE, 1);
                if (ret != 0)
                    throw runtime_error(strprintf("CDB : Failed to configure for no temp file backing for database %s", strFile));
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
                --CDB::bitdb.mapFileUseCount[strFile];
                strFile = "";
                throw runtime_error(strprintf("CDB : Error %d, can't open database %s", ret, strFile));
            }

            if (fCreate && !Exists(string("version"))) {
                bool fTmp = fReadOnly;
                fReadOnly = false;
                WriteVersion(CLIENT_VERSION);
                fReadOnly = fTmp;
            }

            CDB::bitdb.mapDb[strFile] = pdb;
        }
    }
}

void CDB::Flush()
{
    if (activeTxn)
        return;

    // Flush database activity from memory pool to disk log
    unsigned int nMinutes = 0;
    if (fReadOnly)
        nMinutes = 1;

    CDB::bitdb.dbenv.txn_checkpoint(nMinutes ? GetArg("-dblogsize", 100) * 1024 : 0, nMinutes, 0);
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
        LOCK(CDB::bitdb.cs_db);
        --CDB::bitdb.mapFileUseCount[strFile];
    }
}

bool CDB::Rewrite(const string& strFile, const char* pszSkip)
{
    while (true) {
        {
            LOCK(CDB::bitdb.cs_db);
            if (!CDB::bitdb.mapFileUseCount.count(strFile) || CDB::bitdb.mapFileUseCount[strFile] == 0) {
                // Flush log data to the dat file
                CDB::bitdb.CloseDb(strFile);
                CDB::bitdb.CheckpointLSN(strFile);
                CDB::bitdb.mapFileUseCount.erase(strFile);

                bool fSuccess = true;
                LogPrintf("CDB::Rewrite : Rewriting %s...\n", strFile);
                string strFileRes = strFile + ".rewrite";
                { // surround usage of db with extra {}
                    CDB db(strFile.c_str(), "r");
                    Db* pdbCopy = new Db(&CDB::bitdb.dbenv, 0);

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
                        CDB::bitdb.CloseDb(strFile);
                        if (pdbCopy->close(0))
                            fSuccess = false;
                        delete pdbCopy;
                    }
                }
                if (fSuccess) {
                    Db dbA(&CDB::bitdb.dbenv, 0);
                    if (dbA.remove(strFile.c_str(), NULL, 0))
                        fSuccess = false;
                    Db dbB(&CDB::bitdb.dbenv, 0);
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