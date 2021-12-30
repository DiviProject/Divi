// Copyright (c) 2017-2020 The DIVI Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _DEFAULTVALUES_H
#define _DEFAULTVALUES_H

#include <amount.h>

/** Flags for nSequence and nLockTime locks */
enum {
    /* Interpret sequence numbers as relative lock-time constraints. */
    LOCKTIME_VERIFY_SEQUENCE = (1 << 0),

    /* Use GetMedianTimePast() instead of nTime for end point timestamp. */
    LOCKTIME_MEDIAN_TIME_PAST = (1 << 1),
};

/** Used as the flags parameter to sequence and nLocktime checks in non-consensus code. */
constexpr unsigned int STANDARD_LOCKTIME_VERIFY_FLAGS = LOCKTIME_VERIFY_SEQUENCE | LOCKTIME_MEDIAN_TIME_PAST;

/** Default control port */
constexpr char DEFAULT_TOR_CONTROL[] = "127.0.0.1:9051";
constexpr bool DEFAULT_LISTEN_ONION = true;

//! -dbcache default (MiB)
constexpr int64_t DEFAULT_DB_CACHE_SIZE = 100;
//! max. -dbcache in (MiB)
constexpr int64_t MAX_DB_CACHE_SIZE = sizeof(void*) > 4 ? 4096 : 1024;
//! min. -dbcache in (MiB)
constexpr int64_t MIN_DB_CACHE_SIZE = 4;

//! -maxtxfee default
constexpr CAmount DEFAULT_TRANSACTION_MAXFEE = 100 * COIN;

constexpr bool DEFAULT_USE_HD_WALLET = true;

constexpr unsigned int DEFAULT_TX_RELAY_FEE_PER_KILOBYTE = 10000;

constexpr unsigned int MAX_OP_META_RELAY = 603;      //!< bytes (+1 for OP_META, +2 for the pushdata opcodes)

/** The maximum allowed size for a serialized block, in bytes (network rule) */
constexpr unsigned int MAX_BLOCK_SIZE_CURRENT = 2000000;
constexpr unsigned int MAX_BLOCK_SIZE_LEGACY = 1000000;

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
constexpr unsigned int DEFAULT_BLOCK_MAX_SIZE = 750000;
constexpr unsigned int DEFAULT_BLOCK_MIN_SIZE = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
constexpr unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
/** Default for accepting alerts from the P2P network. */
constexpr bool DEFAULT_ALERTS = true;
/** The maximum size for transactions we're willing to relay/mine */
constexpr unsigned int MAX_STANDARD_TX_SIZE = 100000;
/** The maximum allowed number of signature check operations in a block (network rule) */
constexpr unsigned int MAX_BLOCK_SIGOPS_CURRENT = MAX_BLOCK_SIZE_CURRENT / 50;
constexpr unsigned int MAX_BLOCK_SIGOPS_LEGACY = MAX_BLOCK_SIZE_LEGACY / 50;
/** Maximum number of signature check operations in an IsStandard() P2SH script */
constexpr unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
constexpr unsigned int MAX_TX_SIGOPS_CURRENT = MAX_BLOCK_SIGOPS_CURRENT / 5;
constexpr unsigned int MAX_TX_SIGOPS_LEGACY = MAX_BLOCK_SIGOPS_LEGACY / 5;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
constexpr unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** The maximum size of a blk?????.dat file (since 0.8) */
constexpr unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
constexpr unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
constexpr unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
constexpr int COINBASE_MATURITY = 100;
/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
constexpr unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC
/** Maximum number of script-checking threads allowed */
constexpr int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
constexpr int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
constexpr int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
constexpr unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached their tip. Changing this value is a protocol upgrade. */
constexpr unsigned int MAX_HEADERS_RESULTS = 2000;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
constexpr unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blockchain state to disk. */
constexpr unsigned int DATABASE_WRITE_INTERVAL = 3600;
/** Maximum length of reject messages. */
constexpr unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;

constexpr bool DEFAULT_ADDRESSINDEX = false;
constexpr bool DEFAULT_SPENTINDEX = false;

/** Enable bloom filter */
 constexpr bool DEFAULT_PEERBLOOMFILTERS = true;

/** "reject" message codes */
constexpr unsigned char REJECT_MALFORMED = 0x01;
constexpr unsigned char REJECT_INVALID = 0x10;
constexpr unsigned char REJECT_OBSOLETE = 0x11;
constexpr unsigned char REJECT_DUPLICATE = 0x12;
constexpr unsigned char REJECT_NONSTANDARD = 0x40;
constexpr unsigned char REJECT_DUST = 0x41;
constexpr unsigned char REJECT_INSUFFICIENTFEE = 0x42;
constexpr unsigned char REJECT_CHECKPOINT = 0x43;

#endif