#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests basic behaviour (standardness, fees) of OP_META transactions.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *
from PowToPosTransition import createPoSStacks, generatePoSBlocks

import codecs
from decimal import Decimal


class OpMetaTest (BitcoinTestFramework):

    def setup_network (self, split=False):
        args = ["-debug"]
        self.nodes = start_nodes (1, self.options.tmpdir, extra_args=[args])
        self.node = self.nodes[0]
        self.is_network_split = False

    def build_op_meta_tx (self, utxos, payload, fee):
        """
        Builds a transaction with an OP_META output that has the given
        payload data (byte string) and the given absolute fee.  We use
        one of the UTXOs from the passed-in list (as per listunspent)
        and remove it from the list. Returned is the constructed transaction
        as hex string.

        Before actually building the OP_META transaction, the chosen UTXO
        will be spent in a normal transaction first; this ensures that the
        resulting OP_META transaction does not have any "priority" and will
        not be accepted as a free transaction (i.e. that we can test proper
        fee requirements for it).
        """

        if type (payload) != list:
          payload = [payload]

        required = Decimal ('1.00000000') + fee
        inp = None
        for i in range (len (utxos)):
          if utxos[i]["amount"] >= required:
            inp = utxos[i]
            del utxos[i]
            break
        assert inp is not None, "found no suitable output"

        # Spend the chosen output back to us to create a low-priority
        # input for the actual transaction.
        change = Decimal (inp["amount"]) - fee
        assert_greater_than (change, 0)
        changeAddr = self.node.getnewaddress ()
        tx = self.node.createrawtransaction ([inp], {changeAddr: change})
        signed = self.node.signrawtransaction (tx)
        assert_equal (signed["complete"], True)
        txid = self.node.sendrawtransaction (signed["hex"])
        inp["txid"] = txid
        inp["vout"] = 0
        inp["amount"] = change

        # Now build the actual OP_META transaction.
        change = int ((Decimal (inp["amount"]) - fee) * COIN)
        assert_greater_than (change, 0)
        changeAddr = self.node.getnewaddress ()
        data = self.node.validateaddress (changeAddr)

        tx = CTransaction ()
        tx.vin.append (CTxIn (COutPoint (txid=inp["txid"], n=inp["vout"])))
        tx.vout.append (CTxOut (change, codecs.decode (data["scriptPubKey"], "hex")))

        for p in payload:
          meta = CScript ([OP_META, p])
          tx.vout.append (CTxOut (0, meta))

        unsigned = tx.serialize ().hex ()
        signed = self.node.signrawtransaction (unsigned)
        assert_equal (signed["complete"], True)

        decoded = self.node.decoderawtransaction (signed["hex"])

        return signed["hex"], decoded["txid"]

    def check_confirmed (self, txid):
        if type (txid) == list:
          for t in txid:
            self.check_confirmed (t)
          return

        assert_greater_than (self.node.gettransaction (txid)["confirmations"], 0)

    def find_min_fee (self, utxos, payload):
        """
        Runs a binary search on what fee is needed to accept
        an OP_META transaction with one of the UTXOs and the
        given payload.
        """

        def is_sufficient (fee):
          tx, _ = self.build_op_meta_tx (utxos, payload, fee)
          try:
            self.node.sendrawtransaction (tx)
            return True
          except JSONRPCException as exc:
            assert_equal (exc.error["code"], -26)
            return False

        eps = Decimal ('0.00000001')
        lower = eps
        while not is_sufficient (2 * lower):
          lower *= 2
        upper = 2 * lower

        while upper - lower > eps:
          mid = ((upper + lower) / 2).quantize (eps)
          assert mid > lower
          assert upper > mid
          if is_sufficient (mid):
            upper = mid
          else:
            lower = mid

        return upper

    def run_test (self):
        # Advance to PoS blocks to make sure we have the correct
        # fee rules active.
        print ("Advancing to PoS blocks...")
        createPoSStacks ([self.node], self.nodes)
        generatePoSBlocks (self.nodes, 0, 100)

        # Test sending OP_META transactions with a size around the limit
        # for being a standard transaction.  Larger ones are still valid
        # for inside a block, just not accepted to the mempool by default.
        print ("Testing OP_META standardness size limit...")
        utxos = self.node.listunspent ()
        standard, txid1 = self.build_op_meta_tx (utxos, b"x" * 599, Decimal ('0.1'))
        nonstandard, txid2 = self.build_op_meta_tx (utxos, b"x" * 600, Decimal ('0.1'))
        self.node.sendrawtransaction (standard)
        assert_raises (JSONRPCException, self.node.sendrawtransaction,
                       nonstandard)
        self.node.generateblock ({"extratx": [nonstandard]})
        self.check_confirmed ([txid1, txid2])

        # More than one OP_META output is not standard in any case.
        print ("Testing multiple OP_META outputs...")
        utxos = self.node.listunspent ()
        nonstandard, txid = self.build_op_meta_tx (utxos, [b"abc", b"def"], Decimal ('0.1'))
        assert_raises (JSONRPCException, self.node.sendrawtransaction,
                       nonstandard)
        self.node.generateblock ({"extratx": [nonstandard]})
        self.check_confirmed (txid)

        # Check what fee is required for small and large OP_META transactions.
        print ("Checking required fees...")
        utxos = self.node.listunspent ()
        for p in [b"x", b"x" * 599]:
          fee = self.find_min_fee (utxos, p)
          print ("For payload size %d: %.8f DIVI" % (len (p), fee))


if __name__ == '__main__':
    OpMetaTest ().main ()
