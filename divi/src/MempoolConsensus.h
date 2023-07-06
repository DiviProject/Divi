#ifndef MEMPOOL_CONSENSUS_H
#define MEMPOOL_CONSENSUS_H
#include <string>
class CTransaction;
class CCoinsViewCache;
class CValidationState;
class CTxMemPool;
namespace MempoolConsensus
{
    /**
     * Check transaction inputs, and make sure any
     * pay-to-script-hash transactions are evaluating IsStandard scripts
     *
     * Why bother? To avoid denial-of-service attacks; an attacker
     * can submit a standard HASH... OP_EQUAL transaction,
     * which will get accepted into blocks. The redemption
     * script can be anything; an attacker could use a very
     * expensive-to-check-upon-redemption script like:
     *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
     */

    /**
     * Check for standard transaction types
     * @param[in] mapInputs    Map of previous transactions that have outputs we're spending
     * @return True if all inputs (scriptSigs) use only standard transaction forms
     */
    bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs);
    /** Check for standard transaction types
     * @return True if all outputs (scriptPubKeys) use only standard transaction forms
     */
    bool IsStandardTx(const CTransaction& tx, std::string& reason);
    /** (try to) add transaction to memory pool **/
    bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs = nullptr, bool ignoreFees = false);
}
#endif// MEMPOOL_CONSENSUS_H