#ifndef SECP256_K1_CONTEXT_H
#define SECP256_K1_CONTEXT_H
#include <secp256k1.h>
#include <secp256k1_recovery.h>

class Secp256k1Context
{
private:
    secp256k1_context* verifying_context = NULL;
    secp256k1_context* signing_context = NULL;
public:
    Secp256k1Context();
    ~Secp256k1Context();

    secp256k1_context* GetVerifyContext();
    secp256k1_context* GetSigningContext();
};
#endif //SECP256_K1_CONTEXT_H