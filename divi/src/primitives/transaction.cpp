// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "chain.h"
#include "hash.h"
#include "main.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "transaction.h"

#include <boost/foreach.hpp>



COutPoint::COutPoint() { SetNull(); }
COutPoint::COutPoint(uint256 hashIn, uint32_t nIn) { hash = hashIn; n = nIn; }
void COutPoint::SetNull() { hash.SetNull(); n = (uint32_t) -1; }
bool COutPoint::IsNull() const { return (hash.IsNull() && n == (uint32_t) -1); }
bool operator<(const COutPoint& a, const COutPoint& b)
{
    return (a.hash < b.hash || (a.hash == b.hash && a.n < b.n));
}

bool operator==(const COutPoint& a, const COutPoint& b)
{
    return (a.hash == b.hash && a.n == b.n);
}

bool operator!=(const COutPoint& a, const COutPoint& b)
{
    return !(a == b);
}

std::string COutPoint::ToString() const
{
    return strprintf("COutPoint(%s, %u)", hash.ToString()/*.substr(0,10)*/, n);
}

std::string COutPoint::ToStringShort() const
{
    return strprintf("%s-%u", hash.ToString().substr(0,64), n);
}

uint256 COutPoint::GetHash() const
{
    return Hash(BEGIN(hash), END(hash), BEGIN(n), END(n));
}

CTxIn::CTxIn()
{
    nSequence = std::numeric_limits<uint32_t>::max();
}

bool CTxIn::IsFinal() const
{
    return (nSequence == std::numeric_limits<uint32_t>::max());
}

bool operator==(const CTxIn& a, const CTxIn& b)
{
    return (a.prevout   == b.prevout &&
            a.scriptSig == b.scriptSig &&
            a.nSequence == b.nSequence);
}

bool operator!=(const CTxIn& a, const CTxIn& b)
{
    return !(a == b);
}

CTxIn::CTxIn(COutPoint prevoutIn, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = prevoutIn;
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

CTxIn::CTxIn(uint256 hashPrevTx, uint32_t nOut, CScript scriptSigIn, uint32_t nSequenceIn)
{
    prevout = COutPoint(hashPrevTx, nOut);
    scriptSig = scriptSigIn;
    nSequence = nSequenceIn;
}

std::string CTxIn::ToString() const
{
    std::string str;
    str += "CTxIn(";
    str += prevout.ToString();
    if (prevout.IsNull())
        str += strprintf(", coinbase %s", HexStr(scriptSig));
    else
        str += strprintf(", scriptSig=%s", scriptSig.ToString().substr(0,24));
    if (nSequence != std::numeric_limits<unsigned int>::max())
        str += strprintf(", nSequence=%u", nSequence);
    str += ")";
    return str;
}

CTxOut::CTxOut()
{
    SetNull();
}

void CTxOut::SetNull()
{
    nValue = -1;
    scriptPubKey.clear();
}

bool CTxOut::IsNull() const
{
    return (nValue == -1);
}

void CTxOut::SetEmpty()
{
    nValue = 0;
    scriptPubKey.clear();
}

bool CTxOut::IsEmpty() const
{
    return (nValue == 0 && scriptPubKey.empty());
}

bool operator==(const CTxOut& a, const CTxOut& b)
{
    return (a.nValue       == b.nValue &&
            a.scriptPubKey == b.scriptPubKey);
}

bool operator!=(const CTxOut& a, const CTxOut& b)
{
    return !(a == b);
}

CTxOut::CTxOut(const CAmount& nValueIn, CScript scriptPubKeyIn)
{
    nValue = nValueIn;
    scriptPubKey = scriptPubKeyIn;
}

uint256 CTxOut::GetHash() const
{
    return SerializeHash(*this);
}

std::string CTxOut::ToString() const
{
    return strprintf("CTxOut(nValue=%d.%08d, scriptPubKey=%s)", nValue / COIN, nValue % COIN, scriptPubKey.ToString().substr(0,30));
}

bool CTransaction::IsNull() const {
    return vin.empty() && vout.empty();
}

const uint256& CTransaction::GetHash() const {
    return hash;
}

bool CTransaction::IsCoinBase() const
{
    return (vin.size() == 1 && vin[0].prevout.IsNull());
}

bool CTransaction::IsCoinStake() const
{
    // ppcoin: the coin stake transaction is marked with the first output empty
    return (vin.size() > 0 && (!vin[0].prevout.IsNull()) && vout.size() >= 2 && vout[0].IsEmpty());
}

bool operator==(const CTransaction& a, const CTransaction& b)
{
    return a.hash == b.hash;
}

bool operator!=(const CTransaction& a, const CTransaction& b)
{
    return a.hash != b.hash;
}

uint256 CMutableTransaction::GetBareTxid() const
{
    return CTransaction(*this).GetBareTxid();
}

bool operator==(const CMutableTransaction& a, const CMutableTransaction& b)
{
    return a.GetHash() == b.GetHash();
}

bool operator!=(const CMutableTransaction& a, const CMutableTransaction& b)
{
    return !(a == b);
}

CMutableTransaction::CMutableTransaction() : nVersion(CTransaction::CURRENT_VERSION), nLockTime(0) {}
CMutableTransaction::CMutableTransaction(const CTransaction& tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime) {}

uint256 CMutableTransaction::GetHash() const
{
    return SerializeHash(*this);
}

std::string CMutableTransaction::ToString() const
{
    std::string str;
    str += strprintf("CMutableTransaction(ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}

CAmount CMutableTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it)
    {
        // DIVI: previously MoneyRange() was called here. This has been replaced with negative check and boundary wrap check.
        if (it->nValue < 0)
            return -1;

        if ((nValueOut + it->nValue) < nValueOut)
            return -1;

        nValueOut += it->nValue;
    }
    return nValueOut;
}

void CTransaction::UpdateHash() const
{
    *const_cast<uint256*>(&hash) = SerializeHash(*this);
}

uint256 CTransaction::GetBareTxid () const
{
    if (IsCoinBase())
    {
        /* For coinbase transactions, the bare txid equals the normal one.
           They don't contain a real signature anyway, but the scriptSig
           is needed to distinguish them and make sure we won't have two
           transactions with the same bare txid.

           In practice on mainnet, this has no influence, since no more
           coinbases are created after the fork activation (since the network
           is on PoS for a long time).  We still need this here to make sure
           all works fine in tests and is just correct in general.  */
        return GetHash();
    }

    CMutableTransaction withoutSigs(*this);
    for (auto& in : withoutSigs.vin)
        in.scriptSig.clear();
    return withoutSigs.GetHash();
}

CTransaction::CTransaction() : hash(), nVersion(CTransaction::CURRENT_VERSION), vin(), vout(), nLockTime(0) { }

CTransaction::CTransaction(const CMutableTransaction &tx) : nVersion(tx.nVersion), vin(tx.vin), vout(tx.vout), nLockTime(tx.nLockTime) {
    UpdateHash();
}

CTransaction& CTransaction::operator=(const CTransaction &tx) {
    *const_cast<int*>(&nVersion) = tx.nVersion;
    *const_cast<std::vector<CTxIn>*>(&vin) = tx.vin;
    *const_cast<std::vector<CTxOut>*>(&vout) = tx.vout;
    *const_cast<unsigned int*>(&nLockTime) = tx.nLockTime;
    *const_cast<uint256*>(&hash) = tx.hash;
    return *this;
}

CAmount CTransaction::GetValueOut() const
{
    CAmount nValueOut = 0;
    for (std::vector<CTxOut>::const_iterator it(vout.begin()); it != vout.end(); ++it)
    {
        // DIVI: previously MoneyRange() was called here. This has been replaced with negative check and boundary wrap check.
        if (it->nValue < 0)
            throw std::runtime_error("CTransaction::GetValueOut() : value out of range : less than 0");

        if ((nValueOut + it->nValue) < nValueOut)
            throw std::runtime_error("CTransaction::GetValueOut() : value out of range : wraps the int64_t boundary");

        nValueOut += it->nValue;
    }
    return nValueOut;
}

std::string CTransaction::ToString() const
{
    std::string str;
    str += strprintf("CTransaction(hash=%s, ver=%d, vin.size=%u, vout.size=%u, nLockTime=%u)\n",
        GetHash().ToString().substr(0,10),
        nVersion,
        vin.size(),
        vout.size(),
        nLockTime);
    for (unsigned int i = 0; i < vin.size(); i++)
        str += "    " + vin[i].ToString() + "\n";
    for (unsigned int i = 0; i < vout.size(); i++)
        str += "    " + vout[i].ToString() + "\n";
    return str;
}
