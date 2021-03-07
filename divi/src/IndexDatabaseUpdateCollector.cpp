#include <IndexDatabaseUpdateCollector.h>

#include <IndexDatabaseUpdates.h>
#include <addressindex.h>
#include <spentindex.h>
#include <primitives/transaction.h>
#include <vector>
#include <coins.h>

extern bool fAddressIndex;
extern bool fSpentIndex;

namespace Spending
{
void CollectUpdatesFromInputs(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationRef,
    const CCoinsViewCache& view,
    IndexDatabaseUpdates& indexDatabaseUpdates)
{
    if (tx.IsCoinBase()) return;
    if (fAddressIndex || fSpentIndex)
    {
        for (size_t j = 0; j < tx.vin.size(); j++) {

            const CTxIn input = tx.vin[j];
            const CTxOut &prevout = view.GetOutputFor(tx.vin[j]);
            uint160 hashBytes;
            int addressType;

            if (prevout.scriptPubKey.IsPayToScriptHash()) {
                hashBytes = uint160(std::vector<unsigned char>(prevout.scriptPubKey.begin()+2, prevout.scriptPubKey.begin()+22));
                addressType = 2;
            } else if (prevout.scriptPubKey.IsPayToPublicKeyHash()) {
                hashBytes = uint160(std::vector<unsigned char>(prevout.scriptPubKey.begin()+3, prevout.scriptPubKey.begin()+23));
                addressType = 1;
            } else {
                hashBytes.SetNull();
                addressType = 0;
            }
            if (fAddressIndex && addressType > 0) {
                // record spending activity
                indexDatabaseUpdates.addressIndex.push_back(std::make_pair(CAddressIndexKey(addressType, hashBytes, txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, j, true), prevout.nValue * -1));
                // remove address from unspent index
                indexDatabaseUpdates.addressUnspentIndex.push_back(std::make_pair(CAddressUnspentKey(addressType, hashBytes, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
            }
            if (fSpentIndex) {
                // add the spent index to determine the txid and input that spent an output
                // and to find the amount and address from an input
                indexDatabaseUpdates.spentIndex.push_back(std::make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txLocationRef.hash, j, txLocationRef.blockHeight, prevout.nValue, addressType, hashBytes)));
            }
        }
    }
}

void CollectUpdatesFromOutputs(
    const CTransaction& tx,
    const TransactionLocationReference& txLocationRef,
    IndexDatabaseUpdates& indexDatabaseUpdates)
{
    if (fAddressIndex) {
        for (unsigned int k = 0; k < tx.vout.size(); k++) {
            const CTxOut &out = tx.vout[k];

            if (out.scriptPubKey.IsPayToScriptHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);

                // record receiving activity
                indexDatabaseUpdates.addressIndex.push_back(
                    std::make_pair(CAddressIndexKey(2, uint160(hashBytes), txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, k, false), out.nValue));

                // record unspent output
                indexDatabaseUpdates.addressUnspentIndex.push_back(
                    std::make_pair(CAddressUnspentKey(2, uint160(hashBytes), txLocationRef.hash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, txLocationRef.blockHeight)));

            } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                std::vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);

                // record receiving activity
                indexDatabaseUpdates.addressIndex.push_back(
                    std::make_pair(CAddressIndexKey(1, uint160(hashBytes), txLocationRef.blockHeight, txLocationRef.transactionIndex, txLocationRef.hash, k, false), out.nValue));

                // record unspent output
                indexDatabaseUpdates.addressUnspentIndex.push_back(
                    std::make_pair(CAddressUnspentKey(1, uint160(hashBytes), txLocationRef.hash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, txLocationRef.blockHeight)));

            } else {
                continue;
            }

        }
    }
}
}//anonymous namespace

void IndexDatabaseUpdateCollector::RecordTransaction(
        const CTransaction& tx,
        const TransactionLocationReference& txLocationRef,
        const CCoinsViewCache& view,
        IndexDatabaseUpdates& indexDatabaseUpdates)
{
    Spending::CollectUpdatesFromInputs(tx,txLocationRef,view, indexDatabaseUpdates);
    Spending::CollectUpdatesFromOutputs(tx,txLocationRef,indexDatabaseUpdates);
}