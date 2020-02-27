#include "Secp256k1Context.h"

#include <assert.h>
#include "random.h"
#include "allocators.h"

Secp256k1Context::Secp256k1Context()
{
    assert(verifying_context == NULL);
    verifying_context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    assert(verifying_context != NULL);


    assert(signing_context == NULL);
    secp256k1_context *ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    assert(ctx != NULL);
    {
        // Pass in a random blinding seed to the secp256k1 context.
        std::vector<unsigned char, secure_allocator<unsigned char>> vseed(32);
        GetRandBytes(vseed.data(), 32);
        bool ret = secp256k1_context_randomize(ctx, vseed.data());
        assert(ret);
    }
    signing_context = ctx;
}
Secp256k1Context::~Secp256k1Context()
{
    assert(verifying_context != NULL);
    secp256k1_context_destroy(verifying_context);
    verifying_context = NULL;

    secp256k1_context *ctx = signing_context;
    signing_context = NULL;
    if (ctx) {
        secp256k1_context_destroy(ctx);
    }
}

secp256k1_context* Secp256k1Context::GetVerifyContext()
{
    return verifying_context;
}
secp256k1_context* Secp256k1Context::GetSigningContext()
{
    return signing_context;
}