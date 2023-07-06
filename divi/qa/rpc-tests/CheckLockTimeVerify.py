#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the BIP65 (OP_CHECKLOCKTIMEVERIFY) opcode and fork activation.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *

ACTIVATION_TIME = 1_692_792_000


class CheckLockTimeVerifyTest (BitcoinTestFramework):

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

    def buildSpend (self, output):
        """Builds a transaction that spends one of our test outputs."""

        (txid, n) = output
        outpoint = COutPoint (txid=txid, n=n)

        scriptSig = CScript ([42, self.cltvScript])

        tx = CTransaction ()
        tx.vin.append (CTxIn (outpoint, scriptSig))
        tx.vout.append (CTxOut (1, CScript ([OP_META])))

        return tx

    def run_test (self):
        set_node_times (self.nodes, ACTIVATION_TIME - 1_000)

        # Generate outputs locked to block height 100, but spendable
        # easily (by pushing 42 on the stack) afterwards.
        self.node.setgenerate ( 30)
        self.cltvScript = CScript ([100, OP_CHECKLOCKTIMEVERIFY, OP_DROP,
                                   42, OP_EQUAL])
        addr = self.node.decodescript (self.cltvScript.hex ())["p2sh"]
        outputs = [self.generateOutput (addr, 1) for _ in range (2)]
        self.node.setgenerate ( 1)

        # Spending an output before the fork is activated should work
        # even without a proper lock time on the transaction, although
        # mempool policy will not allow it.
        tx = self.buildSpend (outputs[0])
        assert_raises (JSONRPCException, self.node.sendrawtransaction,
                       tx.serialize ().hex ())
        self.node.generateblock ({"extratx": [tx.serialize ().hex ()]})

        # After the fork, the output should not be spendable (even directly
        # in a block) without the lock time.
        set_node_times (self.nodes, ACTIVATION_TIME)
        self.node.setgenerate ( 7)
        tx = self.buildSpend (outputs[1])
        assert_raises (JSONRPCException, self.node.generateblock,
                       {"extratx": [tx.serialize ().hex ()]})

        # With the lock time, it should still not be spendable before
        # the actual block height.
        tx.nLockTime = 100
        assert_greater_than(100, self.node.getblockcount())
        assert_raises (JSONRPCException, self.node.generateblock,
                       {"extratx": [tx.serialize ().hex ()]})

        # Once the lock height is reached (exceeded), the transaction should
        # be fine, even in the mempool.
        additional_blocks = 100 - self.node.getblockcount()
        self.node.setgenerate ( additional_blocks-1)
        assert_raises (JSONRPCException, self.node.generateblock, {"extratx": [tx.serialize ().hex ()]})
        self.node.setgenerate ( 1)
        txid = self.node.sendrawtransaction (tx.serialize ().hex ())
        self.node.setgenerate ( 1)
        assert_greater_than(self.node.getrawtransaction(txid,1)["confirmations"],0)

        # Make sure the outputs have really been spent.
        assert_equal (self.node.getrawmempool (), [])
        for o in outputs:
          assert_equal (self.node.gettxout (o[0], o[1]), None)


if __name__ == '__main__':
    CheckLockTimeVerifyTest ().main ()
