#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the transaction index for lookups of on-disk transactions
# (with -txindex enabled).

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from util import *


class TxIndexTest (BitcoinTestFramework):

    def setup_network (self):
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args+["-txindex=1"]))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args+["-txindex=0"]))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False
        self.sync_all ()

    def expect_found (self, node, txid):
        data = node.getrawtransaction (txid, 1)
        assert_equal (data["txid"], txid)

    def expect_not_found (self, node, txid):
        assert_raises (JSONRPCException, node.getrawtransaction, txid)

    def run_test (self):
        self.nodes[0].setgenerate (True, 30)
        sync_blocks (self.nodes)

        # Lookup of transactions proceeds based on three main avenues:
        # The mempool, the txindex and the UTXO set.  We start off
        # with a test transaction that has one unspent output.  It should
        # be found either from the mempool or the UTXO set independent
        # of -txindex.  When we spend its output, it will only be able
        # to be found with -txindex.

        print ("Creating test transaction...")
        value, inputs = gather_inputs (self.nodes[0], 10)
        addr = self.nodes[0].getnewaddress ()
        tx = self.nodes[0].createrawtransaction (inputs, {addr: value - 1})
        signed = self.nodes[0].signrawtransaction (tx)
        assert_equal (signed["complete"], True)
        txid = self.nodes[0].sendrawtransaction (signed["hex"])
        sync_mempools (self.nodes)

        print ("Lookup through mempool...")
        for n in self.nodes:
          assert_equal (n.getrawmempool (), [txid])
          self.expect_found (n, txid)

        print ("Lookup through UTXO set...")
        self.nodes[0].setgenerate (True, 1)
        sync_blocks (self.nodes)
        for n in self.nodes:
          assert_equal (n.getrawmempool (), [])
          assert n.gettxout (txid, 0) is not None
          self.expect_found (n, txid)

        print ("Spending test output...")
        tx = self.nodes[0].createrawtransaction ([{"txid": txid, "vout": 0}], {addr: value - 2})
        signed = self.nodes[0].signrawtransaction (tx)
        assert_equal (signed["complete"], True)
        self.nodes[0].sendrawtransaction (signed["hex"])
        self.nodes[0].setgenerate (True, 1)
        sync_blocks (self.nodes)

        print ("Lookup through tx index...")
        for n in self.nodes:
          assert_equal (n.getrawmempool (), [])
          assert_equal (n.gettxout (txid, 0), None)
        self.expect_found (self.nodes[0], txid)
        self.expect_not_found (self.nodes[1], txid)


if __name__ == '__main__':
    TxIndexTest ().main ()
