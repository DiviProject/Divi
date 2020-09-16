// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_SCRIPT_H
#define BITCOIN_SCRIPT_SCRIPT_H

#include <assert.h>
#include <climits>
#include <limits>
#include <stdexcept>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>

#include <script/opcodes.h>


class CPubKey;

typedef std::vector<unsigned char> valtype;

static const unsigned int MAX_SCRIPT_ELEMENT_SIZE = 520; // bytes

template <typename T>
std::vector<unsigned char> ToByteVector(const T& in)
{
    return std::vector<unsigned char>(in.begin(), in.end());
}

class scriptnum_error : public std::runtime_error
{
public:
    explicit scriptnum_error(const std::string& str) : std::runtime_error(str) {}
};

class CScriptNum
{
/**
 * Numeric opcodes (OP_1ADD, etc) are restricted to operating on 4-byte integers.
 * The semantics are subtle, though: operands must be in the range [-2^31 +1...2^31 -1],
 * but results may overflow (and are valid as long as they are not used in a subsequent
 * numeric operation). CScriptNum enforces those semantics by storing results as
 * an int64 and allowing out-of-range values to be returned as a vector of bytes but
 * throwing an exception if arithmetic is done or the result is interpreted as an integer.
 */
public:

    explicit CScriptNum(const int64_t& n)
    {
        m_value = n;
    }

    explicit CScriptNum(const std::vector<unsigned char>& vch, bool fRequireMinimal)
    {
        if (vch.size() > nMaxNumSize) {
            throw scriptnum_error("script number overflow");
        }
        if (fRequireMinimal && vch.size() > 0) {
            // Check that the number is encoded with the minimum possible
            // number of bytes.
            //
            // If the most-significant-byte - excluding the sign bit - is zero
            // then we're not minimal. Note how this test also rejects the
            // negative-zero encoding, 0x80.
            if ((vch.back() & 0x7f) == 0) {
                // One exception: if there's more than one byte and the most
                // significant bit of the second-most-significant-byte is set
                // it would conflict with the sign bit. An example of this case
                // is +-255, which encode to 0xff00 and 0xff80 respectively.
                // (big-endian).
                if (vch.size() <= 1 || (vch[vch.size() - 2] & 0x80) == 0) {
                    throw scriptnum_error("non-minimally encoded script number");
                }
            }
        }
        m_value = set_vch(vch);
    }

    inline bool operator==(const int64_t& rhs) const    { return m_value == rhs; }
    inline bool operator!=(const int64_t& rhs) const    { return m_value != rhs; }
    inline bool operator<=(const int64_t& rhs) const    { return m_value <= rhs; }
    inline bool operator< (const int64_t& rhs) const    { return m_value <  rhs; }
    inline bool operator>=(const int64_t& rhs) const    { return m_value >= rhs; }
    inline bool operator> (const int64_t& rhs) const    { return m_value >  rhs; }

    inline bool operator==(const CScriptNum& rhs) const { return operator==(rhs.m_value); }
    inline bool operator!=(const CScriptNum& rhs) const { return operator!=(rhs.m_value); }
    inline bool operator<=(const CScriptNum& rhs) const { return operator<=(rhs.m_value); }
    inline bool operator< (const CScriptNum& rhs) const { return operator< (rhs.m_value); }
    inline bool operator>=(const CScriptNum& rhs) const { return operator>=(rhs.m_value); }
    inline bool operator> (const CScriptNum& rhs) const { return operator> (rhs.m_value); }

    inline CScriptNum operator+(   const int64_t& rhs)    const { return CScriptNum(m_value + rhs);}
    inline CScriptNum operator-(   const int64_t& rhs)    const { return CScriptNum(m_value - rhs);}
    inline CScriptNum operator+(   const CScriptNum& rhs) const { return operator+(rhs.m_value);   }
    inline CScriptNum operator-(   const CScriptNum& rhs) const { return operator-(rhs.m_value);   }

    inline CScriptNum& operator+=( const CScriptNum& rhs)       { return operator+=(rhs.m_value);  }
    inline CScriptNum& operator-=( const CScriptNum& rhs)       { return operator-=(rhs.m_value);  }

    inline CScriptNum operator-() const
    {
        assert(m_value != std::numeric_limits<int64_t>::min());
        return CScriptNum(-m_value);
    }

    inline CScriptNum& operator=( const int64_t& rhs)
    {
        m_value = rhs;
        return *this;
    }

    inline CScriptNum& operator+=( const int64_t& rhs)
    {
        assert(rhs == 0 || (rhs > 0 && m_value <= std::numeric_limits<int64_t>::max() - rhs) ||
                           (rhs < 0 && m_value >= std::numeric_limits<int64_t>::min() - rhs));
        m_value += rhs;
        return *this;
    }

    inline CScriptNum& operator-=( const int64_t& rhs)
    {
        assert(rhs == 0 || (rhs > 0 && m_value >= std::numeric_limits<int64_t>::min() + rhs) ||
                           (rhs < 0 && m_value <= std::numeric_limits<int64_t>::max() + rhs));
        m_value -= rhs;
        return *this;
    }

    int getint() const
    {
        if (m_value > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        else if (m_value < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return m_value;
    }

    std::vector<unsigned char> getvch() const
    {
        return serialize(m_value);
    }

    static std::vector<unsigned char> serialize(const int64_t& value)
    {
        if(value == 0)
            return std::vector<unsigned char>();

        std::vector<unsigned char> result;
        const bool neg = value < 0;
        uint64_t absvalue = neg ? -value : value;

        while(absvalue)
        {
            result.push_back(absvalue & 0xff);
            absvalue >>= 8;
        }

//    - If the most significant byte is >= 0x80 and the value is positive, push a
//    new zero-byte to make the significant byte < 0x80 again.

//    - If the most significant byte is >= 0x80 and the value is negative, push a
//    new 0x80 byte that will be popped off when converting to an integral.

//    - If the most significant byte is < 0x80 and the value is negative, add
//    0x80 to it, since it will be subtracted and interpreted as a negative when
//    converting to an integral.

        if (result.back() & 0x80)
            result.push_back(neg ? 0x80 : 0);
        else if (neg)
            result.back() |= 0x80;

        return result;
    }

    static const size_t nMaxNumSize = 4;

private:
    static int64_t set_vch(const std::vector<unsigned char>& vch)
    {
      if (vch.empty())
          return 0;

      int64_t result = 0;
      for (size_t i = 0; i != vch.size(); ++i)
          result |= static_cast<int64_t>(vch[i]) << 8*i;

      // If the input vector's most significant byte is 0x80, remove it from
      // the result's msb and return a negative.
      if (vch.back() & 0x80)
          return -((int64_t)(result & ~(0x80ULL << (8 * (vch.size() - 1)))));

      return result;
    }

    int64_t m_value;
};

/** Serialized script, used inside transaction inputs and outputs */
class CScript : public std::vector<unsigned char>
{
protected:
    CScript& push_int64(int64_t n)
    {
        if (n == -1 || (n >= 1 && n <= 16))
        {
            push_back(n + (OP_1 - 1));
        }
        else if (n == 0)
        {
            push_back(OP_0);
        }
        else
        {
            *this << CScriptNum::serialize(n);
        }
        return *this;
    }
public:
    CScript() { }
    CScript(const CScript& b) : std::vector<unsigned char>(b.begin(), b.end()) { }
    CScript(const_iterator pbegin, const_iterator pend) : std::vector<unsigned char>(pbegin, pend) { }
    CScript(const unsigned char* pbegin, const unsigned char* pend) : std::vector<unsigned char>(pbegin, pend) { }

    CScript& operator+=(const CScript& b)
    {
        insert(end(), b.begin(), b.end());
        return *this;
    }

    friend CScript operator+(const CScript& a, const CScript& b)
    {
        CScript ret = a;
        ret += b;
        return ret;
    }

    CScript(int64_t b)        { operator<<(b); }

    explicit CScript(opcodetype b)     { operator<<(b); }
    explicit CScript(const CScriptNum& b) { operator<<(b); }
    explicit CScript(const std::vector<unsigned char>& b) { operator<<(b); }


    CScript& operator<<(int64_t b) { return push_int64(b); }

    CScript& operator<<(opcodetype opcode)
    {
        if (opcode < 0 || opcode > 0xff)
            throw std::runtime_error("CScript::operator<<() : invalid opcode");
        insert(end(), (unsigned char)opcode);
        return *this;
    }

    CScript& operator<<(const CScriptNum& b)
    {
        *this << b.getvch();
        return *this;
    }

    CScript& operator<<(const std::vector<unsigned char>& b)
    {
        if (b.size() < OP_PUSHDATA1)
        {
            insert(end(), (unsigned char)b.size());
        }
        else if (b.size() <= 0xff)
        {
            insert(end(), OP_PUSHDATA1);
            insert(end(), (unsigned char)b.size());
        }
        else if (b.size() <= 0xffff)
        {
            insert(end(), OP_PUSHDATA2);
            unsigned short nSize = b.size();
            insert(end(), (unsigned char*)&nSize, (unsigned char*)&nSize + sizeof(nSize));
        }
        else
        {
            insert(end(), OP_PUSHDATA4);
            unsigned int nSize = b.size();
            insert(end(), (unsigned char*)&nSize, (unsigned char*)&nSize + sizeof(nSize));
        }
        insert(end(), b.begin(), b.end());
        return *this;
    }

    CScript& operator<<(const CScript& b)
    {
        // I'm not sure if this should push the script or concatenate scripts.
        // If there's ever a use for pushing a script onto a script, delete this member fn
        assert(!"Warning: Pushing a CScript onto a CScript with << is probably not intended, use + to concatenate!");
        return *this;
    }

    CScript& operator<<(const CPubKey& key);

    /** Pushes a given data vector in the minimal possible way.  This uses
     *  OP_1 to OP_16 as well as some others to do single-byte pushes
     *  for some cases (unlike operator<<).  The result will comply with
     *  SCRIPT_VERIFY_MINIMALDATA.  A notable situation where this can
     *  not be used instead of operator<< is the genesis block scriptsig.  */
    CScript& PushMinimal(const std::vector<unsigned char>& b)
    {
        if (b.size() == 0) {
            return *this << OP_0;
        }

        if (b.size() == 1 && b[0] >= 1 && b[0] <= 16) {
            return *this << static_cast<opcodetype>(OP_1 + (b[0] - 1));
        }

        if (b.size() == 1 && b[0] == 0x81) {
            return *this << OP_1NEGATE;
        }

        return *this << b;
    }


    bool GetOp(iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet)
    {
         // Wrapper so it can be called with either iterator or const_iterator
         const_iterator pc2 = pc;
         bool fRet = GetOp2(pc2, opcodeRet, &vchRet);
         pc = begin() + (pc2 - begin());
         return fRet;
    }

    bool GetOp(iterator& pc, opcodetype& opcodeRet)
    {
         const_iterator pc2 = pc;
         bool fRet = GetOp2(pc2, opcodeRet, NULL);
         pc = begin() + (pc2 - begin());
         return fRet;
    }

    bool GetOp(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>& vchRet) const
    {
        return GetOp2(pc, opcodeRet, &vchRet);
    }

    bool GetOp(const_iterator& pc, opcodetype& opcodeRet) const
    {
        return GetOp2(pc, opcodeRet, NULL);
    }

    bool GetOp2(const_iterator& pc, opcodetype& opcodeRet, std::vector<unsigned char>* pvchRet) const
    {
        opcodeRet = OP_INVALIDOPCODE;
        if (pvchRet)
            pvchRet->clear();
        if (pc >= end())
            return false;

        // Read instruction
        if (end() - pc < 1)
            return false;
        unsigned int opcode = *pc++;

        // Immediate operand
        if (opcode <= OP_PUSHDATA4)
        {
            unsigned int nSize = 0;
            if (opcode < OP_PUSHDATA1)
            {
                nSize = opcode;
            }
            else if (opcode == OP_PUSHDATA1)
            {
                if (end() - pc < 1)
                    return false;
                nSize = *pc++;
            }
            else if (opcode == OP_PUSHDATA2)
            {
                if (end() - pc < 2)
                    return false;
                nSize = 0;
                memcpy(&nSize, &pc[0], 2);
                pc += 2;
            }
            else if (opcode == OP_PUSHDATA4)
            {
                if (end() - pc < 4)
                    return false;
                memcpy(&nSize, &pc[0], 4);
                pc += 4;
            }
            if (end() - pc < 0 || (unsigned int)(end() - pc) < nSize)
                return false;
            if (pvchRet)
                pvchRet->assign(pc, pc + nSize);
            pc += nSize;
        }

        opcodeRet = (opcodetype)opcode;
        return true;
    }

    /** Encode/decode small integers: */
    static int DecodeOP_N(opcodetype opcode)
    {
        if (opcode == OP_0)
            return 0;
        assert(opcode >= OP_1 && opcode <= OP_16);
        return (int)opcode - (int)(OP_1 - 1);
    }
    static opcodetype EncodeOP_N(int n)
    {
        assert(n >= 0 && n <= 16);
        if (n == 0)
            return OP_0;
        return (opcodetype)(OP_1+n-1);
    }

    int FindAndDelete(const CScript& b)
    {
        int nFound = 0;
        if (b.empty())
            return nFound;
        iterator pc = begin();
        opcodetype opcode;
        do
        {
            while (end() - pc >= (long)b.size() && memcmp(&pc[0], &b[0], b.size()) == 0)
            {
                pc = erase(pc, pc + b.size());
                ++nFound;
            }
        }
        while (GetOp(pc, opcode));
        return nFound;
    }
    int Find(opcodetype op) const
    {
        int nFound = 0;
        opcodetype opcode;
        for (const_iterator pc = begin(); pc != end() && GetOp(pc, opcode);)
            if (opcode == op)
                ++nFound;
        return nFound;
    }

    /**
     * Pre-version-0.6, Bitcoin always counted CHECKMULTISIGs
     * as 20 sigops. With pay-to-script-hash, that changed:
     * CHECKMULTISIGs serialized in scriptSigs are
     * counted more accurately, assuming they are of the form
     *  ... OP_N CHECKMULTISIG ...
     */
    unsigned int GetSigOpCount(bool fAccurate) const;

    /**
     * Accurately count sigOps, including sigOps in
     * pay-to-script-hash transactions:
     */
    unsigned int GetSigOpCount(const CScript& scriptSig) const;

    bool IsNormalPaymentScript() const;
    bool IsPayToScriptHash() const;
    bool IsPayToPublicKeyHash() const;

    /** Called by IsStandardTx and P2SH/BIP62 VerifyScript (which makes it consensus-critical). */
    bool IsPushOnly(const_iterator pc) const;
    bool IsPushOnly() const;

    /**
     * Returns whether the script is guaranteed to fail at execution,
     * regardless of the initial stack. This allows outputs to be pruned
     * instantly when entering the UTXO set.
     */
    bool IsUnspendable() const
    {
        return (size() > 0 && *begin() == OP_META);
    }

    std::string ToString() const;
    void clear()
    {
        // The default std::vector::clear() does not release memory.
        std::vector<unsigned char>().swap(*this);
    }
};

#endif // BITCOIN_SCRIPT_SCRIPT_H
