#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test prunning wallet transactions to keep only spendable ones with depth less than <NumberOfConfirmations>
#

from test_framework import BitcoinTestFramework
from util import *
from messages import *
from script import *
import codecs
from decimal import Decimal
class PruneWallet (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args+["-autocombine=0"]))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False
        self.miner = self.nodes[0]
        self.non_miner = self.nodes[1]

    def restart_non_miner_with_flags(self,added_flags = []):
        stop_node(self.nodes[1],1)
        self.non_miner = None
        self.nodes[1] = None
        flags = ["-debug","-stakesplitthreshold"] + added_flags
        self.nodes[1] = start_node(1, self.options.tmpdir, extra_args= flags)
        self.non_miner = self.nodes[1]

    def run_test(self):
        addr_to_spend_from = self.non_miner.getnewaddress()
        tx_count = 0
        for _ in range(100):
            self.miner.setgenerate(1)
            if self.miner.getbalance() > 1200.0 and tx_count < 30:
                self.miner.sendtoaddress(addr_to_spend_from, 1200)
                tx_count +=1
        sync_blocks(self.nodes)
        assert_equal(self.miner.getblockcount(),100)
        assert_equal(self.non_miner.getblockcount(),100)
        additional_blocks = 30
        self.non_miner.sendtoaddress(addr_to_spend_from,self.non_miner.getbalance() - Decimal(1.0))
        sync_mempools(self.nodes)
        self.miner.setgenerate(additional_blocks)
        sync_blocks(self.nodes)
        expected_block_count = 100 + additional_blocks
        assert_equal(self.miner.getblockcount(),expected_block_count)

        tx_list = self.non_miner.listtransactions("*",200)
        assert_equal(len(tx_list),tx_count+1)
        self.restart_non_miner_with_flags(added_flags=["-prunewalletconfs=25"])
        sync_blocks(self.nodes,timeout=25.0)
        assert_equal(self.non_miner.getblockcount(),expected_block_count)
        tx_list = self.non_miner.listtransactions("*",200)
        print("Tx list: \n{}\n".format(tx_list))
        assert_equal(len(tx_list),2) # One entry per utxo: one for last send, one for change amount
        self.restart_non_miner_with_flags()
        assert_equal(self.non_miner.getblockcount(),expected_block_count)
        tx_list = self.non_miner.listtransactions("*",200)
        assert_equal(len(tx_list),tx_count+1)



if __name__ == '__main__':
    PruneWallet().main ()
