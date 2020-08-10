#include <script/SignatureCheckers.h>
#include <script/script.h>
#include <script/script_error.h>
#include "primitives/transaction.h"
#include "script/scriptandsigflags.h"
#include <eccryptoverify.h>
#include <pubkey.h>

MutableTransactionSignatureChecker::MutableTransactionSignatureChecker(
    const CMutableTransaction* txToIn,
    unsigned int nInIn
    ) : TransactionSignatureChecker(NULL, nInIn)
    , txToPtr(std::make_shared<const CTransaction>(*txToIn))
{
    txTo = txToPtr.get();
}

bool static IsCompressedOrUncompressedPubKey(const valtype &vchPubKey) {
    if (vchPubKey.size() < 33) {
        //  Non-canonical public key: too short
        return false;
    }
    if (vchPubKey[0] == 0x04) {
        if (vchPubKey.size() != 65) {
            //  Non-canonical public key: invalid length for uncompressed key
            return false;
        }
    } else if (vchPubKey[0] == 0x02 || vchPubKey[0] == 0x03) {
        if (vchPubKey.size() != 33) {
            //  Non-canonical public key: invalid length for compressed key
            return false;
        }
    } else {
          //  Non-canonical public key: neither compressed nor uncompressed
          return false;
    }
    return true;
}

/**
 * A canonical signature exists of: <30> <total len> <02> <len R> <R> <02> <len S> <S> <hashtype>
 * Where R and S are not negative (their first byte has its highest bit not set), and not
 * excessively padded (do not start with a 0 byte, unless an otherwise negative number follows,
 * in which case a single 0 byte is necessary and even required).
 *
 * See https://bitcointalk.org/index.php?topic=8392.msg127623#msg127623
 *
 * This function is consensus-critical since BIP66.
 */
bool static IsValidSignatureEncoding(const std::vector<unsigned char> &sig) {
    // Format: 0x30 [total-length] 0x02 [R-length] [R] 0x02 [S-length] [S] [sighash]
    // * total-length: 1-byte length descriptor of everything that follows,
    //   excluding the sighash byte.
    // * R-length: 1-byte length descriptor of the R value that follows.
    // * R: arbitrary-length big-endian encoded R value. It must use the shortest
    //   possible encoding for a positive integers (which means no null bytes at
    //   the start, except a single one when the next byte has its highest bit set).
    // * S-length: 1-byte length descriptor of the S value that follows.
    // * S: arbitrary-length big-endian encoded S value. The same rules apply.
    // * sighash: 1-byte value indicating what data is hashed (not part of the DER
    //   signature)

    // Minimum and maximum size constraints.
    if (sig.size() < 9) return false;
    if (sig.size() > 73) return false;

    // A signature is of type 0x30 (compound).
    if (sig[0] != 0x30) return false;

    // Make sure the length covers the entire signature.
    if (sig[1] != sig.size() - 3) return false;

    // Extract the length of the R element.
    unsigned int lenR = sig[3];

    // Make sure the length of the S element is still inside the signature.
    if (5 + lenR >= sig.size()) return false;

    // Extract the length of the S element.
    unsigned int lenS = sig[5 + lenR];

    // Verify that the length of the signature matches the sum of the length
    // of the elements.
    if ((size_t)(lenR + lenS + 7) != sig.size()) return false;

    // Check whether the R element is an integer.
    if (sig[2] != 0x02) return false;

    // Zero-length integers are not allowed for R.
    if (lenR == 0) return false;

    // Negative numbers are not allowed for R.
    if (sig[4] & 0x80) return false;

    // Null bytes at the start of R are not allowed, unless R would
    // otherwise be interpreted as a negative number.
    if (lenR > 1 && (sig[4] == 0x00) && !(sig[5] & 0x80)) return false;

    // Check whether the S element is an integer.
    if (sig[lenR + 4] != 0x02) return false;

    // Zero-length integers are not allowed for S.
    if (lenS == 0) return false;

    // Negative numbers are not allowed for S.
    if (sig[lenR + 6] & 0x80) return false;

    // Null bytes at the start of S are not allowed, unless S would otherwise be
    // interpreted as a negative number.
    if (lenS > 1 && (sig[lenR + 6] == 0x00) && !(sig[lenR + 7] & 0x80)) return false;

    return true;
}

bool static IsLowDERSignature(const valtype &vchSig, ScriptError* serror) {
    if (!IsValidSignatureEncoding(vchSig)) {
        return set_error(serror, SCRIPT_ERR_SIG_DER);
    }
    unsigned int nLenR = vchSig[3];
    unsigned int nLenS = vchSig[5+nLenR];
    const unsigned char *S = &vchSig[6+nLenR];
    // If the S value is above the order of the curve divided by two, its
    // complement modulo the order could have been used instead, which is
    // one byte shorter when encoded correctly.
    if (!eccrypto::CheckSignatureElement(S, nLenS, true))
        return set_error(serror, SCRIPT_ERR_SIG_HIGH_S);

    return true;
}

bool static IsDefinedHashtypeSignature(const valtype &vchSig) {
    if (vchSig.size() == 0) {
        return false;
    }
    unsigned char nHashType = vchSig[vchSig.size() - 1] & (~(SIGHASH_ANYONECANPAY));
    if (nHashType < SIGHASH_ALL || nHashType > SIGHASH_SINGLE)
        return false;

    return true;
}

bool BaseSignatureChecker::CheckSignatureEncoding(const std::vector<unsigned char> &vchSig, unsigned int flags, ScriptError* serror) {
    // Empty signature. Not strictly DER encoded, but allowed to provide a
    // compact way to provide an invalid signature for use with CHECK(MULTI)SIG
    if (vchSig.size() == 0) {
        return true;
    }
    if ((flags & (SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC)) != 0 && !IsValidSignatureEncoding(vchSig)) {
        return set_error(serror, SCRIPT_ERR_SIG_DER);
    } else if ((flags & SCRIPT_VERIFY_LOW_S) != 0 && !IsLowDERSignature(vchSig, serror)) {
        // serror is set
        return false;
    } else if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 && !IsDefinedHashtypeSignature(vchSig)) {
        return set_error(serror, SCRIPT_ERR_SIG_HASHTYPE);
    }
    return true;
}

bool BaseSignatureChecker::CheckPubKeyEncoding(const std::vector<unsigned char> &vchSig, unsigned int flags, ScriptError* serror) {
    if ((flags & SCRIPT_VERIFY_STRICTENC) != 0 && !IsCompressedOrUncompressedPubKey(vchSig)) {
        return set_error(serror, SCRIPT_ERR_PUBKEYTYPE);
    }
    return true;
}


namespace {

/**
 * Wrapper that serializes like CTransaction, but with the modifications
 *  required for the signature hash done in-place
 */
class CTransactionSignatureSerializer {
private:
    const CTransaction &txTo;  //! reference to the spending transaction (the one being serialized)
    const CScript &scriptCode; //! output script being consumed
    const unsigned int nIn;    //! input index of txTo being signed
    const bool fAnyoneCanPay;  //! whether the hashtype has the SIGHASH_ANYONECANPAY flag set
    const bool fHashSingle;    //! whether the hashtype is SIGHASH_SINGLE
    const bool fHashNone;      //! whether the hashtype is SIGHASH_NONE

public:
    CTransactionSignatureSerializer(const CTransaction &txToIn, const CScript &scriptCodeIn, unsigned int nInIn, int nHashTypeIn) :
        txTo(txToIn), scriptCode(scriptCodeIn), nIn(nInIn),
        fAnyoneCanPay(!!(nHashTypeIn & SIGHASH_ANYONECANPAY)),
        fHashSingle((nHashTypeIn & 0x1f) == SIGHASH_SINGLE),
        fHashNone((nHashTypeIn & 0x1f) == SIGHASH_NONE) {}

    /** Serialize the passed scriptCode, skipping OP_CODESEPARATORs */
    template<typename S>
    void SerializeScriptCode(S &s, int nType, int nVersion) const {
        CScript::const_iterator it = scriptCode.begin();
        CScript::const_iterator itBegin = it;
        opcodetype opcode;
        unsigned int nCodeSeparators = 0;
        while (scriptCode.GetOp(it, opcode)) {
            if (opcode == OP_CODESEPARATOR)
                nCodeSeparators++;
        }
        ::WriteCompactSize(s, scriptCode.size() - nCodeSeparators);
        it = itBegin;
        while (scriptCode.GetOp(it, opcode)) {
            if (opcode == OP_CODESEPARATOR) {
                s.write((char*)&itBegin[0], it-itBegin-1);
                itBegin = it;
            }
        }
        if (itBegin != scriptCode.end())
            s.write((char*)&itBegin[0], it-itBegin);
    }

    /** Serialize an input of txTo */
    template<typename S>
    void SerializeInput(S &s, unsigned int nInput, int nType, int nVersion) const {
        // In case of SIGHASH_ANYONECANPAY, only the input being signed is serialized
        if (fAnyoneCanPay)
            nInput = nIn;
        // Serialize the prevout
        ::Serialize(s, txTo.vin[nInput].prevout, nType, nVersion);
        // Serialize the script
        if (nInput != nIn)
            // Blank out other inputs' signatures
            ::Serialize(s, CScript(), nType, nVersion);
        else
            SerializeScriptCode(s, nType, nVersion);
        // Serialize the nSequence
        if (nInput != nIn && (fHashSingle || fHashNone))
            // let the others update at will
            ::Serialize(s, (int)0, nType, nVersion);
        else
            ::Serialize(s, txTo.vin[nInput].nSequence, nType, nVersion);
    }

    /** Serialize an output of txTo */
    template<typename S>
    void SerializeOutput(S &s, unsigned int nOutput, int nType, int nVersion) const {
        if (fHashSingle && nOutput != nIn)
            // Do not lock-in the txout payee at other indices as txin
            ::Serialize(s, CTxOut(), nType, nVersion);
        else
            ::Serialize(s, txTo.vout[nOutput], nType, nVersion);
    }

    /** Serialize txTo */
    template<typename S>
    void Serialize(S &s, int nType, int nVersion) const {
        // Serialize nVersion
        ::Serialize(s, txTo.nVersion, nType, nVersion);
        // Serialize vin
        unsigned int nInputs = fAnyoneCanPay ? 1 : txTo.vin.size();
        ::WriteCompactSize(s, nInputs);
        for (unsigned int nInput = 0; nInput < nInputs; nInput++)
             SerializeInput(s, nInput, nType, nVersion);
        // Serialize vout
        unsigned int nOutputs = fHashNone ? 0 : (fHashSingle ? nIn+1 : txTo.vout.size());
        ::WriteCompactSize(s, nOutputs);
        for (unsigned int nOutput = 0; nOutput < nOutputs; nOutput++)
             SerializeOutput(s, nOutput, nType, nVersion);
        // Serialize nLockTime
        ::Serialize(s, txTo.nLockTime, nType, nVersion);
    }
};

} // anon namespace

uint256 SignatureHash(const CScript& scriptCode, const CTransaction& txTo, unsigned int nIn, int nHashType)
{
    if (nIn >= txTo.vin.size()) {
        //  nIn out of range
        return 1;
    }

    // Check for invalid use of SIGHASH_SINGLE
    if ((nHashType & 0x1f) == SIGHASH_SINGLE) {
        if (nIn >= txTo.vout.size()) {
            //  nOut out of range
            return 1;
        }
    }

    // Wrapper to serialize only the necessary parts of the transaction being signed
    CTransactionSignatureSerializer txTmp(txTo, scriptCode, nIn, nHashType);

    // Serialize and hash
    CHashWriter ss(SER_GETHASH, 0);
    ss << txTmp << nHashType;
    return ss.GetHash();
}

bool TransactionSignatureChecker::VerifySignature(const std::vector<unsigned char>& vchSig, const CPubKey& pubkey, const uint256& sighash) const
{
    return pubkey.Verify(sighash, vchSig);
}

bool TransactionSignatureChecker::CheckSig(const std::vector<unsigned char>& vchSigIn, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode) const
{
    CPubKey pubkey(vchPubKey);
    if (!pubkey.IsValid())
        return false;

    // Hash type is one byte tacked on to the end of the signature
    std::vector<unsigned char> vchSig(vchSigIn);
    if (vchSig.empty())
        return false;
    int nHashType = vchSig.back();
    vchSig.pop_back();

    uint256 sighash = SignatureHash(scriptCode, *txTo, nIn, nHashType);

    if (!VerifySignature(vchSig, pubkey, sighash))
        return false;

    return true;
}