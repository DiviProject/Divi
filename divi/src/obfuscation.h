// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OBFUSCATION_H
#define OBFUSCATION_H

#include "main.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "obfuscation-relay.h"
#include "sync.h"

class CTxIn;
class CObfuscationPool;
class CObfuScationSigner;
class CMasterNodeVote;
class CBitcoinAddress;
class CObfuscationQueue;
class CObfuscationBroadcastTx;
// class CActiveMasternode;

// pool states for mixing
#define POOL_STATUS_UNKNOWN 0              // waiting for update
#define POOL_STATUS_IDLE 1                 // waiting for update
#define POOL_STATUS_QUEUE 2                // waiting in a queue
#define POOL_STATUS_ACCEPTING_ENTRIES 3    // accepting entries
#define POOL_STATUS_FINALIZE_TRANSACTION 4 // master node will broadcast what it accepted
#define POOL_STATUS_SIGNING 5              // check inputs/outputs, sign final tx
#define POOL_STATUS_TRANSMISSION 6         // transmit transaction
#define POOL_STATUS_ERROR 7                // error
#define POOL_STATUS_SUCCESS 8              // success

// status update message constants
#define MASTERNODE_ACCEPTED 1
#define MASTERNODE_REJECTED 0
#define MASTERNODE_RESET -1

#define OBFUSCATION_QUEUE_TIMEOUT 30
#define OBFUSCATION_SIGNING_TIMEOUT 15

// used for anonymous relaying of inputs/outputs/sigs
#define OBFUSCATION_RELAY_IN 1
#define OBFUSCATION_RELAY_OUT 2
#define OBFUSCATION_RELAY_SIG 3

static const CAmount OBFUSCATION_COLLATERAL = (10 * COIN);
static const CAmount OBFUSCATION_POOL_MAX = (99999.99 * COIN);

extern CObfuscationPool obfuScationPool;
extern CObfuScationSigner obfuScationSigner;
extern std::vector<CObfuscationQueue> vecObfuscationQueue;
extern std::string strMasterNodePrivKey;
extern map<uint256, CObfuscationBroadcastTx> mapObfuscationBroadcastTxes;
// extern CActiveMasternode activeMasternode;

/** Holds an Obfuscation input
 */
class CTxDSIn : public CTxIn
{
public:
    bool fHasSig;   // flag to indicate if signed
    int nSentTimes; //times we've sent this anonymously

    CTxDSIn(const CTxIn& in)
    {
        prevout = in.prevout;
        scriptSig = in.scriptSig;
        prevPubKey = in.prevPubKey;
        nSequence = in.nSequence;
        nSentTimes = 0;
        fHasSig = false;
    }
};

/** Holds an Obfuscation output
 */
class CTxDSOut : public CTxOut
{
public:
    int nSentTimes; //times we've sent this anonymously

    CTxDSOut(const CTxOut& out)
    {
        nValue = out.nValue;
        nRounds = out.nRounds;
        scriptPubKey = out.scriptPubKey;
        nSentTimes = 0;
    }
};

// A clients transaction in the obfuscation pool
class CObfuScationEntry
{
public:
    bool isSet;
    std::vector<CTxDSIn> sev;
    std::vector<CTxDSOut> vout;
    CAmount amount;
    CTransaction collateral;
    CTransaction txSupporting;
    int64_t addedTime; // time in UTC milliseconds

    CObfuScationEntry()
    {
        isSet = false;
        collateral = CTransaction();
        amount = 0;
    }

    /// Add entries to use for Obfuscation
    bool Add(const std::vector<CTxIn> vinIn, int64_t amountIn, const CTransaction collateralIn, const std::vector<CTxOut> voutIn)
    {
        if (isSet) {
            return false;
        }

        BOOST_FOREACH (const CTxIn& in, vinIn)
            sev.push_back(in);

        BOOST_FOREACH (const CTxOut& out, voutIn)
            vout.push_back(out);

        amount = amountIn;
        collateral = collateralIn;
        isSet = true;
        addedTime = GetTime();

        return true;
    }

    bool AddSig(const CTxIn& vin)
    {
        BOOST_FOREACH (CTxDSIn& s, sev) {
            if (s.prevout == vin.prevout && s.nSequence == vin.nSequence) {
                if (s.fHasSig) {
                    return false;
                }
                s.scriptSig = vin.scriptSig;
                s.prevPubKey = vin.prevPubKey;
                s.fHasSig = true;

                return true;
            }
        }

        return false;
    }

    bool IsExpired()
    {
        return (GetTime() - addedTime) > OBFUSCATION_QUEUE_TIMEOUT; // 120 seconds
    }
};


/**
 * A currently inprogress Obfuscation merge and denomination information
 */
class CObfuscationQueue
{
public:
    CTxIn vin;
    int64_t time;
    int nDenom;
    bool ready; //ready for submit
    std::vector<unsigned char> vchSig;

    CObfuscationQueue()
    {
        nDenom = 0;
        vin = CTxIn();
        time = 0;
        vchSig.clear();
        ready = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(nDenom);
        READWRITE(vin);
        READWRITE(time);
        READWRITE(ready);
        READWRITE(vchSig);
    }

    bool GetAddress(CService& addr)
    {
        // CMasternode* pmn = mnodeman.Find(vin);
        if (mnodeman.my != NULL) {
            addr = mnodeman.my->service;
            return true;
        }
        return false;
    }

    /// Get the protocol version
    bool GetProtocolVersion(int& protocolVersion)
    {
        // CMasternode* pmn = mnodeman.Find(vin);
        if (mnodeman.my != NULL) {
            protocolVersion = mnodeman.my->protocolVersion;
            return true;
        }
        return false;
    }

    /** Sign this Obfuscation transaction
     *  \return true if all conditions are met:
     *     1) we have an active Masternode,
     *     2) we have a valid Masternode private key,
     *     3) we signed the message successfully, and
     *     4) we verified the message successfully
     */
    bool Sign();

    bool Relay();

    /// Is this Obfuscation expired?
    bool IsExpired()
    {
        return (GetTime() - time) > OBFUSCATION_QUEUE_TIMEOUT; // 120 seconds
    }

    /// Check if we have a valid Masternode address
    bool CheckSignature();
};

/** Helper class to store Obfuscation transaction (tx) information.
 */
class CObfuscationBroadcastTx
{
public:
    CTransaction tx;
    CTxIn vin;
    vector<unsigned char> vchSig;
    int64_t sigTime;
};

/** Helper object for signing and checking signatures
 */
class CObfuScationSigner
{
public:
    /// Is the inputs associated with this public key? (and there is 10000 DIV - checking if valid masternode)
    bool IsVinAssociatedWithPubkey(CTxIn& vin, CPubKey& pubkey);
    /// Set the private/public key values, returns true if successful
    bool GetKeysFromSecret(std::string strSecret, CKey& keyRet, CPubKey& pubkeyRet);
    /// Set the private/public key values, returns true if successful
    bool SetKey(std::string strSecret, std::string& errorMessage, CKey& key, CPubKey& pubkey);
    /// Sign the message, returns true if successful
    bool SignMessage(std::string strMessage, std::string& errorMessage, std::vector<unsigned char>& vchSig, CKey key);
    /// Verify the message, returns true if succcessful
    bool VerifyMessage(CPubKey pubkey, std::vector<unsigned char>& vchSig, std::string strMessage, std::string& errorMessage);
};

/** Used to keep track of current status of Obfuscation pool
 */
class CObfuscationPool
{
private:
    mutable CCriticalSection cs_obfuscation;

    std::vector<CObfuScationEntry> entries; // Masternode/clients entries
    CMutableTransaction finalTransaction;   // the finalized transaction ready for signing

    int64_t lastTimeChanged; // last time the 'state' changed, in UTC milliseconds

    unsigned int state; // should be one of the POOL_STATUS_XXX values
    unsigned int entriesCount;
    unsigned int lastEntryAccepted;
    unsigned int countEntriesAccepted;

    std::vector<CTxIn> lockedCoins;

    std::string lastMessage;
    bool unitTest;

    int sessionID;

    int sessionUsers;            //N Users have said they'll join
    bool sessionFoundMasternode; //If we've found a compatible Masternode
    std::vector<CTransaction> vecSessionCollateral;

    int cachedLastSuccess;

    int minBlockSpacing; //required blocks between mixes
    CMutableTransaction txCollateral;

    int64_t lastNewBlock;

    //debugging data
    std::string strAutoDenomResult;

public:
    enum messages {
        ERR_ALREADY_HAVE,
        ERR_DENOM,
        ERR_ENTRIES_FULL,
        ERR_EXISTING_TX,
        ERR_FEES,
        ERR_INVALID_COLLATERAL,
        ERR_INVALID_INPUT,
        ERR_INVALID_SCRIPT,
        ERR_INVALID_TX,
        ERR_MAXIMUM,
        ERR_MN_LIST,
        ERR_MODE,
        ERR_NON_STANDARD_PUBKEY,
        ERR_NOT_A_MN,
        ERR_QUEUE_FULL,
        ERR_RECENT,
        ERR_SESSION,
        ERR_MISSING_TX,
        ERR_VERSION,
        MSG_NOERR,
        MSG_SUCCESS,
        MSG_ENTRIES_ADDED
    };

    // where collateral should be made out to
    CScript collateralPubKey;

    CMasternode* pSubmittedToMasternode;
    int sessionDenom;    //Users must submit an denom matching this
    int cachedNumBlocks; //used for the overview screen

    CObfuscationPool()
    {
        /* Obfuscation uses collateral addresses to trust parties entering the pool
            to behave themselves. If they don't it takes their money. */

        cachedLastSuccess = 0;
        cachedNumBlocks = std::numeric_limits<int>::max();
        unitTest = false;
        txCollateral = CMutableTransaction();
        minBlockSpacing = 0;
        lastNewBlock = 0;

        SetNull();
    }

    /** Process a Obfuscation message using the Obfuscation protocol
     * \param pfrom
     * \param strCommand lower case command string; valid values are:
     *        Command  | Description
     *        -------- | -----------------
     *        dsa      | Obfuscation Acceptable
     *        dsc      | Obfuscation Complete
     *        dsf      | Obfuscation Final tx
     *        dsi      | Obfuscation vIn
     *        dsq      | Obfuscation Queue
     *        dss      | Obfuscation Signal Final Tx
     *        dssu     | Obfuscation status update
     *        dssub    | Obfuscation Subscribe To
     * \param vRecv
     */
    void ProcessMessageObfuscation(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void InitCollateralAddress()
    {
        SetCollateralAddress(Params().ObfuscationPoolDummyAddress());
    }

    void SetMinBlockSpacing(int minBlockSpacingIn)
    {
        minBlockSpacing = minBlockSpacingIn;
    }

    bool SetCollateralAddress(std::string strAddress);
    void Reset();
    void SetNull();

    void UnlockCoins();

    bool IsNull() const
    {
        return state == POOL_STATUS_ACCEPTING_ENTRIES && entries.empty();
    }

    int GetState() const
    {
        return state;
    }

    std::string GetStatus();

    int GetEntriesCount() const
    {
        return entries.size();
    }

    /// Get the time the last entry was accepted (time in UTC milliseconds)
    int GetLastEntryAccepted() const
    {
        return lastEntryAccepted;
    }

    /// Get the count of the accepted entries
    int GetCountEntriesAccepted() const
    {
        return countEntriesAccepted;
    }

    // Set the 'state' value, with some logging and capturing when the state changed
    void UpdateState(unsigned int newState)
    {
        if (fMasterNode && (newState == POOL_STATUS_ERROR || newState == POOL_STATUS_SUCCESS)) {
            // LogPrint("obfuscation", "CObfuscationPool::UpdateState() - Can't set state to ERROR or SUCCESS as a Masternode. \n");
            return;
        }

        // LogPrintf("CObfuscationPool::UpdateState() == %d | %d \n", state, newState);
        if (state != newState) {
            lastTimeChanged = GetTimeMillis();
            if (fMasterNode) {
                RelayStatus(obfuScationPool.sessionID, obfuScationPool.GetState(), obfuScationPool.GetEntriesCount(), MASTERNODE_RESET);
            }
        }
        state = newState;
    }

    /// Get the maximum number of transactions for the pool
    int GetMaxPoolTransactions()
    {
        return Params().PoolMaxTransactions();
    }

    /// Do we have enough users to take entries?
    bool IsSessionReady()
    {
        return sessionUsers >= GetMaxPoolTransactions();
    }

    /// Are these outputs compatible with other client in the pool?
    bool IsCompatibleWithEntries(std::vector<CTxOut>& vout);

    /// Is this amount compatible with other client in the pool?
    bool IsCompatibleWithSession(CAmount nAmount, CTransaction txCollateral, int& errorID);

    /// Passively run Obfuscation in the background according to the configuration in settings (only for QT)
    bool DoAutomaticDenominating(bool fDryRun = false);
    bool PrepareObfuscationDenominate();

    /// Check for process in Obfuscation
    void Check();
    void CheckFinalTransaction();
    /// Charge fees to bad actors (Charge clients a fee if they're abusive)
    void ChargeFees();
    /// Rarely charge fees to pay miners
    void ChargeRandomFees();
    void CheckTimeout();
    void CheckForCompleteQueue();
    /// Check to make sure a signature matches an input in the pool
    bool SignatureValid(const CScript& newSig, const CTxIn& newVin);
    /// If the collateral is valid given by a client
    bool IsCollateralValid(const CTransaction& txCollateral);
    /// Add a clients entry to the pool
    bool AddEntry(const std::vector<CTxIn>& newInput, const CAmount& nAmount, const CTransaction& txCollateral, const std::vector<CTxOut>& newOutput, int& errorID);
    /// Add signature to a vin
    bool AddScriptSig(const CTxIn& newVin);
    /// Check that all inputs are signed. (Are all inputs signed?)
    bool SignaturesComplete();
    /// As a client, send a transaction to a Masternode to start the denomination process
    void SendObfuscationDenominate(std::vector<CTxIn>& vin, std::vector<CTxOut>& vout, CAmount amount);
    /// Get Masternode updates about the progress of Obfuscation
    bool StatusUpdate(int newState, int newEntriesCount, int newAccepted, int& errorID, int newSessionID = 0);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(CTransaction& finalTransactionNew, CNode* node);

    /// Get the last valid block hash for a given modulus
    bool GetLastValidBlockHash(uint256& hash, int mod = 1, int nBlockHeight = 0);
    /// Process a new block
    void NewBlock();
    void CompletedTransaction(bool error, int errorID);
    void ClearLastMessage();
    /// Used for liquidity providers
    bool SendRandomPaymentToSelf();

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts();
    bool CreateDenominated(CAmount nTotalValue);

    /// Get the denominations for a list of outputs (returns a bitshifted integer)
    int GetDenominations(const std::vector<CTxOut>& vout, bool fSingleRandomDenom = false);
    int GetDenominations(const std::vector<CTxDSOut>& vout);

    void GetDenominationsToString(int nDenom, std::string& strDenom);

    /// Get the denominations for a specific amount of divi.
    int GetDenominationsByAmount(CAmount nAmount, int nDenomTarget = 0); // is not used anymore?
    int GetDenominationsByAmounts(std::vector<CAmount>& vecAmount);

    std::string GetMessageByID(int messageID);

    //
    // Relay Obfuscation Messages
    //

    void RelayFinalTransaction(const int sessionID, const CTransaction& txNew);
    void RelaySignaturesAnon(std::vector<CTxIn>& vin);
    void RelayInAnon(std::vector<CTxIn>& vin, std::vector<CTxOut>& vout);
    void RelayIn(const std::vector<CTxDSIn>& vin, const CAmount& nAmount, const CTransaction& txCollateral, const std::vector<CTxDSOut>& vout);
    void RelayStatus(const int sessionID, const int newState, const int newEntriesCount, const int newAccepted, const int errorID = MSG_NOERR);
    void RelayCompletedTransaction(const int sessionID, const bool error, const int errorID);
};

void ThreadCheckObfuScationPool();

#endif
