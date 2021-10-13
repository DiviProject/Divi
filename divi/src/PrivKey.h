#ifndef PRIV_KEY_H
#define PRIV_KEY_H
#include <allocators.h>
#include <vector>
/**
 * secure_allocator is defined in allocators.h
 * CPrivKey is a serialized private key, with all parameters included (279 bytes)
 */
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CPrivKey;
#endif// PRIV_KEY_H