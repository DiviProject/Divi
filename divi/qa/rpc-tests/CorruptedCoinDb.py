#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test CoinDB vs. BlockTree inconsistency behaviour repair
#

from test_framework import BitcoinTestFramework
from authproxy import AuthServiceProxy, JSONRPCException
from util import *

class CorruptedCoinDB(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir, ["-debug","-spendzeroconfchange"]))
        self.invalidated_blocks = []

    def stop_and_restart_node(self):
        stop_node (self.nodes[0], 0)
        self.nodes[0] = None
        options = ["-debug","-spendzeroconfchange","-safe_shutdown=0"]
        print("Restarting with options: {}".format(options))
        self.nodes[0] = start_node(0, self.options.tmpdir, options)

    def create_forks(self,fork_count=5):
        for _ in range(fork_count):
            besthash = self.nodes[0].getbestblockhash()
            self.nodes[0].invalidateblock(besthash)
            self.nodes[0].setgenerate(1)
            self.invalidated_blocks.append(besthash)

    def run_test(self):
        self.nodes[0].setgenerate(25)
        non_coinbase_txids = []

        # Make the chain 3 blocks deeper with 5 blocks at each depth
        non_coinbase_tx_address = self.nodes[0].getnewaddress()
        for _ in range(3):
            for _ in range(8):
                txid = self.nodes[0].sendtoaddress(non_coinbase_tx_address,1200.0)
                non_coinbase_txids.append(txid)
            self.nodes[0].setgenerate(1)
            self.create_forks(fork_count=5)

        # Reconsider invalidated blocks
        self.nodes[0].setgenerate(1)
        for hash in self.invalidated_blocks:
            self.nodes[0].reconsiderblock(hash)

        print("Finished setting up forks")
        best_block_height = self.nodes[0].getblockcount()
        best_block_hash = self.nodes[0].getblockhash(best_block_height)
        for height_offset in range(4):
            last_block_hash = self.nodes[0].getblockhash(best_block_height-height_offset)
            self.nodes[0].reverseblocktransactions(last_block_hash)

        print("Transactions reversed. Block count after reversing txs: {}".format(self.nodes[0].getblockcount()))

        print("Transactions reversed. Starting node...")
        self.stop_and_restart_node()
        assert_equal(self.nodes[0].getblockcount(),best_block_height)
        assert_equal(self.nodes[0].getblockhash(best_block_height),best_block_hash)


if __name__ == '__main__':
    CorruptedCoinDB().main()
