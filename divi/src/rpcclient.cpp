// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcclient.h"

#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_utils.h"
#include "json/json_spirit_writer_template.h"

#include <stdint.h>

/** Convert strings to command-specific RPC representation */
json_spirit::Array RPCConvertValues(const std::string& strMethod, const std::vector<std::string>& strParams)
{
    json_spirit::Array params;

    for (unsigned int idx = 0; idx < strParams.size(); idx++) {
        const std::string& strVal = strParams[idx];

        json_spirit::Value jVal;
        if (!read_string(strVal, jVal))
        {
            params.push_back(strVal);
        }
        else
        {
            params.push_back(jVal);
        }
    }

    return params;
}
