// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_RPCCLIENT_H
#define BITCOIN_RPCCLIENT_H

#include "json/json_spirit_value.h"

json_spirit::Array RPCConvertValues(const std::string& strMethod, const std::vector<std::string>& strParams);
#endif // BITCOIN_RPCCLIENT_H
