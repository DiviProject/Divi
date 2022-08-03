#ifndef AVAILABLE_COINS_TYPE_H
#define AVAILABLE_COINS_TYPE_H
enum class AvailableCoinsType {
    ALL_SPENDABLE_COINS = 0,                    // find masternode outputs including locked ones (use with caution)
    STAKABLE_COINS = 1,                          // UTXO's that are valid for staking
    OWNED_VAULT_COINS = 2
};
#endif //AVAILABLE_COINS_TYPE_H