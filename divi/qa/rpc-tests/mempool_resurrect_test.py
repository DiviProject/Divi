#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test resurrection of mined transactions when
# the blockchain is re-organized.
#

from test_framework import BitcoinTestFramework
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from util import *
import os
import shutil

# Create one-input, one-output, no-fee transaction:
class MempoolCoinbaseTest(BitcoinTestFramework):

    def setup_network(self):
        # Just need one node for this test
        args = ["-checkmempool", "-debug=mempool", "-disablesafemode"]
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, args))
        self.is_network_split = False

    def create_tx(self, from_txid, to_address, amount):
        inputs = [{ "txid" : from_txid, "vout" : 0}]
        outputs = { to_address : amount }
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        signresult = self.nodes[0].signrawtransaction(rawtx)
        assert_equal(signresult["complete"], True)
        return signresult["hex"]

    def run_test(self):
        node = self.nodes[0]
        node.setgenerate(True, 30)
        node0_address = node.getnewaddress()

        # Spend block 1/2/3's coinbase transactions
        # Mine a block.
        # Create three more transactions, spending the spends
        # Mine another block.
        # ... make sure all the transactions are confirmed
        # Invalidate both blocks
        # ... make sure all the transactions are put back in the mempool
        # Mine a new block
        # ... make sure all the transactions are confirmed again.

        b = [ node.getblockhash(n) for n in range(1, 4) ]
        coinbase_txids = [ node.getblock(h)['tx'][0] for h in b ]
        spends1_raw = [ self.create_tx(txid, node0_address, 1250) for txid in coinbase_txids ]
        spends1_id = [ node.sendrawtransaction(tx) for tx in spends1_raw ]

        blocks = []
        blocks.extend(node.setgenerate(True, 1))

        spends2_raw = [ self.create_tx(txid, node0_address, 1249.99) for txid in spends1_id ]
        spends2_id = [ node.sendrawtransaction(tx) for tx in spends2_raw ]

        blocks.extend(node.setgenerate(True, 1))

        # mempool should be empty, all txns confirmed
        assert_equal(set(node.getrawmempool()), set())
        for txid in spends1_id+spends2_id:
            tx = node.gettransaction(txid)
            assert(tx["confirmations"] > 0)

        # Use invalidateblock to re-org back; all transactions should
        # end up unconfirmed and back in the mempool
        node.invalidateblock(blocks[0])
        assert_equal(set(node.getrawmempool()), set(spends1_id+spends2_id))
        for txid in spends1_id+spends2_id:
            tx = node.gettransaction(txid)
            assert(tx["confirmations"] == 0)

        # Generate another block, they should all get mined
        node.setgenerate(True, 1)
        # mempool should be empty, all txns confirmed
        assert_equal(set(node.getrawmempool()), set())
        for txid in spends1_id+spends2_id:
            tx = node.gettransaction(txid)
            assert(tx["confirmations"] > 0)


if __name__ == '__main__':
    MempoolCoinbaseTest().main()
