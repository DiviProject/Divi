#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the standardness logic related to transaction inputs and P2SH.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *


class TxInputStandardnessTest (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.nodes = start_nodes (1, self.options.tmpdir)
        self.node = self.nodes[0]
        self.is_network_split = False

    def generateOutput (self, addr, amount):
        """Sends amount DIVI to the given address, and returns
        a (txid, n) pair corresponding to the created output."""

        txid = self.node.sendtoaddress (addr, amount)

        tx = self.node.getrawtransaction (txid, 1)
        for i in range (len (tx["vout"])):
            if tx["vout"][i]["scriptPubKey"]["addresses"] == [addr]:
                return (txid, i)

        raise AssertionError ("failed to find destination address")

    def buildSpend (self, outputs):
        """Builds a transaction that spends the given outputs
        (paying 1 sat to OP_META and otherwise spending in fees
        for simplicity)."""

        tx = CTransaction ()
        tx.vout.append (CTxOut (1, CScript ([OP_META])))

        for o in outputs:
            (txid, n) = o
            outpoint = COutPoint (txid=txid, n=n)
            tx.vin.append (CTxIn (outpoint));

        signed = self.node.signrawtransaction (ToHex (tx))
        # We don't care if the transaction is completely signed yet.

        return FromHex (CTransaction (), signed["hex"])

    def expectNonStandard (self, tx):
        """Expects that a given transaction is valid but non-standard."""
        txHex = ToHex (tx)
        assert_raises (JSONRPCException, self.node.sendrawtransaction, txHex)
        self.node.generateblock ({"extratx": [txHex]})

    def run_test (self):
        self.nodes[0].setgenerate ( 30)

        # We generate two normal P2SH outputs.
        addr = [
            self.node.getnewaddress ()
            for _ in range (2)
        ]
        pk = [
            self.node.validateaddress (a)["pubkey"]
            for a in addr
        ]
        p2sh = [
            self.node.addmultisigaddress (1, [p])
            for p in pk
        ]
        outputs = [
            self.generateOutput (a, 1)
            for a in p2sh
        ]
        self.node.setgenerate ( 1)

        # Also, we create a P2SH output that has an unknown form
        # but is easy to spend.
        eqScript = CScript ([42, OP_EQUAL])
        eqAddr = self.node.decodescript (eqScript.hex ())["p2sh"]
        eqOutput = self.generateOutput (eqAddr, 1)
        self.node.setgenerate ( 1)

        # Spending a P2SH output with a non-standard script fails.
        # This script embeds lots of extra data in the scriptSig,
        # which is non-standard as being extra arguments.
        tx = self.buildSpend ([outputs[0]])
        tx.vin[0].scriptSig = CScript (
          [b"foo"] * 50 + [x for x in CScript (tx.vin[0].scriptSig)]
        )
        self.expectNonStandard (tx)

        # This is also true if we include a valid (considered standard)
        # spend before the non-standard input.
        tx = self.buildSpend ([eqOutput, outputs[1]])
        tx.vin[0].scriptSig = CScript ([
          42, eqScript,
        ])
        tx.vin[1].scriptSig = CScript (
          [b"foo"] * 50 + [x for x in CScript (tx.vin[1].scriptSig)]
        )
        self.expectNonStandard (tx)


if __name__ == '__main__':
    TxInputStandardnessTest ().main ()
