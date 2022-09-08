// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/divi-config.h"
#endif

#include "amount.h"
#include <string>

class ChainstateManager;
class CNode;
class CNodeSignals;

/** Get Current Chain Height with acquired lock **/
int GetHeight();
void RegisterNodeSignals();
void UnregisterNodeSignals();
CNodeSignals& GetNodeSignals();
#endif // BITCOIN_MAIN_H
