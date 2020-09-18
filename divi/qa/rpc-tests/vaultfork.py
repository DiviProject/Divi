#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests the logic around OP_REQUIRE_COINSTAKE that is forked active
# at an activation time.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from util import *

from PowToPosTransition import createPoSStacks, generatePoSBlocks

ACTIVATION_TIME = 2_000_000_000


class VaultForkTest (BitcoinTestFramework):

    def setup_network (self):
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[1], 0)
        self.is_network_split = False
        self.sync_all ()

    def fund_vault (self, owner, staker, amount):
        """
        Sets up a vault between the two nodes with the given amount.
        Returns the vault UTXO as dict with "vout" and "txid" fields.
        """

        txid = owner.fundvault (staker.getnewaddress (), amount)["txhash"]
        outputs = owner.getrawtransaction (txid, 1)["vout"]
        for n in range (len (outputs)):
            if outputs[n]["scriptPubKey"]["type"] == "vault":
                return {"txid": txid, "vout": n}

        raise AssertionError ("constructed transaction has no vault output")

    def sign_and_send (self, node, tx):
        signed = node.signrawtransaction (tx)
        assert_equal (signed["complete"], True)
        return node.sendrawtransaction (signed["hex"])

    def run_test (self):
        # After the big bump in time, we need to make sure the nodes
        # are connected still / again in case they timed out the
        # connection between them.
        set_node_times(self.nodes, ACTIVATION_TIME - 1_000)
        connect_nodes (self.nodes[1], 0)
        self.nodes[0].setgenerate (True, 1)
        sync_blocks (self.nodes)
        createPoSStacks(self.nodes, self.nodes)

        # We need to activate PoS at least for some parts of the test.
        print ("Activating PoS...")
        generatePoSBlocks (self.nodes, 0, 100)

        print ("Spending vault as owner before the fork...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        generatePoSBlocks (self.nodes, 0, 1)
        addr = self.nodes[0].getnewaddress ("unvaulted")
        unsigned = self.nodes[0].createrawtransaction ([vault], {addr: 9})
        self.sign_and_send (self.nodes[0], unsigned)
        generatePoSBlocks (self.nodes, 0, 1)
        assert_equal (self.nodes[0].getbalance ("unvaulted"), 9)

        print ("Spending vault as staker before the fork...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        generatePoSBlocks (self.nodes, 0, 1)
        addr = self.nodes[1].getnewaddress ("stolen")
        unsigned = self.nodes[1].createrawtransaction ([vault], {addr: 9})

        # The signature will be added, even though it is considered as invalid
        # by the applied standard flags (which include OP_REQUIRE_COINSTAKE
        # independent of fork activation).  Also the mempool will not accept it.
        signed = self.nodes[1].signrawtransaction (unsigned)
        assert_equal (signed["complete"], False)
        assert_raises (JSONRPCException, self.nodes[1].sendrawtransaction, signed["hex"])

        # If we include the transaction directly in a block, it is valid.
        self.nodes[0].generateblock ({"extratx": [signed["hex"]]})
        sync_blocks (self.nodes)
        assert_equal (self.nodes[1].getbalance ("stolen"), 9)

        print ("Activating fork...")
        blk = self.nodes[0].getblockheader (self.nodes[0].getbestblockhash ())
        assert_greater_than (ACTIVATION_TIME, blk["time"])
        set_node_times (self.nodes, ACTIVATION_TIME + 1_000)
        self.nodes[0].setgenerate (True, 1)
        connect_nodes (self.nodes[1], 0)
        sync_blocks (self.nodes)
        blk = self.nodes[0].getblockheader (self.nodes[0].getbestblockhash ())
        assert_greater_than (blk["time"], ACTIVATION_TIME)

        print ("Spending vault as staker with activated fork...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        generatePoSBlocks (self.nodes, 1, 1)
        addr = self.nodes[1].getnewaddress ("stolen again")
        unsigned = self.nodes[1].createrawtransaction ([vault], {addr: 9})
        signed = self.nodes[1].signrawtransaction (unsigned)
        assert_raises (JSONRPCException, self.nodes[1].generateblock,
                       {"extratx": [signed["hex"]]})
        assert_equal (self.nodes[1].getbalance ("stolen again"), 0)

        print ("Spending vault as owner after the fork...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        generatePoSBlocks (self.nodes, 1, 1)
        addr = self.nodes[0].getnewaddress ("unvaulted 2")
        unsigned = self.nodes[0].createrawtransaction ([vault], {addr: 9})
        self.sign_and_send (self.nodes[0], unsigned)
        generatePoSBlocks (self.nodes, 1, 1)
        assert_equal (self.nodes[0].getbalance ("unvaulted 2"), 9)


if __name__ == '__main__':
    VaultForkTest ().main ()
