#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the spent index interaction with transactions that are
# only in the mempool.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from util import *

from decimal import Decimal


class SpentIndexMempoolTest (BitcoinTestFramework):

    def setup_network (self):
        args = ["-debug", "-spentindex=1"]
        self.nodes = start_nodes (1, self.options.tmpdir, extra_args=[args])
        self.node = self.nodes[0]
        self.is_network_split = False

    def build_spend_tx (self, utxo):
        """Spends the given output in a transaction, returning
        the signed but not broadcast raw transaction."""

        value = utxo["amount"]
        fee = Decimal ("0.01")

        addr = self.node.getnewaddress ()
        out = {addr: value - fee}

        unsigned = self.node.createrawtransaction ([utxo], out)
        signed = self.node.signrawtransaction (unsigned)
        assert_equal (signed["complete"], True)

        return signed["hex"]

    def run_test (self):
        self.node.setgenerate (True, 30)

        # Pick an UTXO and spend it in the mempool.
        utxo = self.node.listunspent ()[0]
        tx = self.build_spend_tx (utxo)
        txid1 = self.node.sendrawtransaction (tx)
        assert_equal (self.node.getrawmempool (), [txid1])
        spent_input = {"txid": utxo["txid"], "index": utxo["vout"]}
        assert_equal (self.node.getspentinfo (spent_input), {
          "txid": txid1,
          "index": 0,
          "height": -1,
        })

        # Double-spend the UTXO in a block with another transaction.
        # The spent data should reflect that (instead of the original
        # spend from the mempool).
        tx = self.build_spend_tx (utxo)
        txid2 = self.node.decoderawtransaction (tx)["txid"]
        assert txid1 != txid2
        self.node.generateblock ({"extratx": [tx], "ignoreMempool": True})
        assert_equal (self.node.getrawmempool (), [])
        assert_equal (self.node.getspentinfo (spent_input), {
          "txid": txid2,
          "index": 0,
          "height": self.node.getblockcount (),
        })


if __name__ == '__main__':
    SpentIndexMempoolTest ().main ()
