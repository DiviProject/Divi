#ifndef SIGNATURE_CHECKERS_H
#define SIGNATURE_CHECKERS_H

#include <script/script_error.h>

#include <stdint.h>
#include <string>
#include <vector>
#include <memory>

class CPubKey;
class CScript;
class CTransaction;
class uint256;
class CMutableTransaction;

uint256 SignatureHash(const CScript &scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType);

class BaseSignatureChecker
{
public:
    virtual bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const
    {
        return false;
    }

    virtual ~BaseSignatureChecker() {}

    static bool CheckSignatureEncoding(const std::vector<unsigned char> &vchSig, unsigned int flags, ScriptError* serror);
    static bool CheckPubKeyEncoding(const std::vector<unsigned char> &vchSig, unsigned int flags, ScriptError* serror);
};

class TransactionSignatureChecker : public BaseSignatureChecker
{
protected:
    const CTransaction* txTo;
    unsigned int nIn;

protected:
    virtual bool VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& vchPubKey, const uint256& sighash) const;

public:
    TransactionSignatureChecker(const CTransaction* txToIn, unsigned int nInIn) : txTo(txToIn), nIn(nInIn) {}
    bool CheckSig(const std::vector<unsigned char>& scriptSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const;
};

class MutableTransactionSignatureChecker : public TransactionSignatureChecker
{
private:
    std::shared_ptr<const CTransaction> txToPtr;

public:
    MutableTransactionSignatureChecker(const CMutableTransaction* txToIn, unsigned int nInIn);
};
#endif //SIGNATURE_CHECKERS_H