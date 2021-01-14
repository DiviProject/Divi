// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

/**
 * network protocol versioning
 */

//! initial proto version, to be increased after version/verack negotiation
static constexpr int INIT_PROTO_VERSION = 209;

//! In this version, 'getheaders' was introduced.
static constexpr int GETHEADERS_VERSION = 70077;

//! disconnect from peers older than this proto version
static constexpr int MIN_PEER_PROTO_VERSION_AFTER_ENFORCEMENT = 70915;

//! nTime field added to CAddress, starting with this version;
//! if possible, avoid requesting addresses nodes older than this
static constexpr int CADDR_TIME_VERSION = 31402;

//! BIP 0031, pong message, is enabled for all versions AFTER this one
static constexpr int BIP0031_VERSION = 60000;

//! "mempool" command, enhanced "getdata" behavior starts with this version
static constexpr int MEMPOOL_GD_VERSION = 60002;

//! "filter*" commands are disabled without NODE_BLOOM after and including this version
static constexpr int NO_BLOOM_VERSION = 70005;

/** The current protocol version.   Since it can be changed through -protocolversion
 *  for testing, it is not a constexpr but a real variable.  */
extern const int& PROTOCOL_VERSION;

/** Changes the current protocol version (PROTOCOL_VERSION).  This is used for
 *  testing with -protocolversion.  It should not be applied during normal
 *  production use, in which case the default value of PROTOCOL_VERSION is good.  */
void SetProtocolVersion(int newVersion);

/** See whether the protocol update is enforced for connected nodes */
int ActiveProtocol();

#endif // BITCOIN_VERSION_H
