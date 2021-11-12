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


class VaultForkTest (BitcoinTestFramework):

    def setup_network (self):
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
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
        # We need to activate PoS at least for some parts of the test.
        print ("Activating PoS...")
        createPoSStacks(self.nodes, self.nodes)
        generatePoSBlocks (self.nodes, 0, 100)

        print ("Spending vault as staker...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        sync_mempools(self.nodes[:2])
        generatePoSBlocks (self.nodes, 1, 1)
        addr = self.nodes[1].getnewaddress ("stolen again")
        unsigned = self.nodes[1].createrawtransaction ([vault], {addr: 9})
        signed = self.nodes[1].signrawtransaction (unsigned)
        assert_raises (JSONRPCException, self.nodes[1].generateblock,
                       {"extratx": [signed["hex"]]})
        assert_equal (self.nodes[1].getbalance ("stolen again"), 0)

        print ("Spending vault as owner...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10)
        sync_mempools(self.nodes[:2])
        generatePoSBlocks (self.nodes, 1, 1)
        addr = self.nodes[0].getnewaddress ("unvaulted 2")
        unsigned = self.nodes[0].createrawtransaction ([vault], {addr: 9})
        self.sign_and_send (self.nodes[0], unsigned)
        sync_mempools(self.nodes)
        generatePoSBlocks (self.nodes, 1, 1)
        assert_equal (self.nodes[0].getbalance ("unvaulted 2"), 9)

        print ("Trying to steal as the staker...")
        vault = self.fund_vault (self.nodes[0], self.nodes[1], 10_000)
        sync_mempools(self.nodes[:2])
        generatePoSBlocks (self.nodes, 1, 20)
        # Lottery blocks are every 10th on regtest, and treasury blocks
        # the ones after them (eqv 1 mod 10).  We want to avoid them
        # so that we do not have to deal with the rewards payment.
        while (self.nodes[1].getblockcount () + 1) % 10 in [0, 1]:
            generatePoSBlocks (self.nodes, 1, 1)
        bestBlock = self.nodes[1].getblockheader (self.nodes[1].getbestblockhash ())
        set_node_times(self.nodes, bestBlock["time"] + 1_000)
        addr = self.nodes[1].getnewaddress ("stolen by staking")
        # Correct staking rewards for the current block.
        stakingRewards = 456
        outputs = [
            {"": 0},
            {addr: 10_000 + stakingRewards},
        ]
        stake = self.nodes[1].createrawtransaction ([vault], outputs)
        stake = self.nodes[1].signrawtransaction (stake)
        assert_equal (stake["complete"], True)
        assert_raises (JSONRPCException, self.nodes[1].generateblock,
                       {"coinstake": stake["hex"]})
        assert_equal (self.nodes[1].getbalance ("stolen by staking"), 0)

        print ("Staking properly with the vault...")
        vaultOut = self.nodes[1].gettxout (vault["txid"], vault["vout"])
        vaultScript = vaultOut["scriptPubKey"]["hex"]
        outputs = [
            {"": 0},
            {vaultScript: 10_000 + stakingRewards},
        ]
        stake = self.nodes[1].createrawtransaction ([vault], outputs)
        stake = self.nodes[1].signrawtransaction (stake)
        assert_equal (stake["complete"], True)
        self.nodes[1].generateblock ({"coinstake": stake["hex"]})


if __name__ == '__main__':
    VaultForkTest ().main ()
