#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests that rejecting transactions passed on from a peer into the mempool
# works as expected.  In particular, there used to be a bug that triggered
# a lock-order violation when reject messages were sent.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *

from decimal import Decimal
import time


class MempoolRejectTest (BitcoinTestFramework):

    def setup_network (self, split=False):
        base_args = ["-debug=net", "-debug=mempool"]
        # Node 0 accepts non-standard transactions, and node 1 does not.
        args = [base_args + ["-acceptnonstandard"], base_args]
        self.nodes = start_nodes (2, self.options.tmpdir, extra_args=args)
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def run_test (self):
        # To make one node broadcast a transaction that the other node
        # rejects, we use a nonstandard transaction while one of the
        # nodes is set to allow that and the other to reject them
        # according to their relay policy.

        self.nodes[0].setgenerate ( 30)
        utxo = self.nodes[0].listunspent ()[0]

        value = utxo["amount"]
        fee = Decimal ("0.01")

        tx = CTransaction ()
        tx.vin.append (CTxIn (COutPoint (txid=utxo["txid"], n=utxo["vout"])))
        tx.vout.append (CTxOut (int (100_000_000 * (value - fee)),
                                CScript ([1234, OP_EQUAL])))

        unsigned = tx.serialize ().hex ()
        signed = self.nodes[0].signrawtransaction (unsigned)
        assert_equal (signed["complete"], True)

        # The transaction will be rejected by node 1.
        assert_raises (JSONRPCException, self.nodes[1].sendrawtransaction,
                       signed["hex"])
        txid = self.nodes[0].sendrawtransaction (signed["hex"])
        time.sleep (1)
        assert_equal (self.nodes[0].getrawmempool (), [txid])
        assert_equal (self.nodes[1].getrawmempool (), [])


if __name__ == '__main__':
    MempoolRejectTest ().main ()
