#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test the BIP66 changeover logic
#

from test_framework import BitcoinTestFramework
from bitcoinrpc.authproxy import JSONRPCException
from util import *

from PowToPosTransition import createPoSStacks, generatePoSBlocks

class BIP66Test(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.nodes.append(start_node(0, self.options.tmpdir, []))
        self.nodes.append(start_node(1, self.options.tmpdir, ["-blockversion=2"]))
        self.nodes.append(start_node(2, self.options.tmpdir, ["-blockversion=3"]))
        connect_nodes(self.nodes[1], 0)
        connect_nodes(self.nodes[2], 0)
        self.is_network_split = False
        self.sync_all()

    def run_test(self):
        createPoSStacks(self.nodes[1:], self.nodes)
        cnt = self.nodes[0].getblockcount()

        # Mine some old-version blocks
        generatePoSBlocks(self.nodes, 1, 100)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 100):
            raise AssertionError("Failed to mine 100 version=2 blocks")

        # Mine 750 new-version blocks
        generatePoSBlocks(self.nodes, 2, 750)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 850):
            raise AssertionError("Failed to mine 750 version=3 blocks")

        # TODO: check that new DERSIG rules are not enforced

        # Mine 1 new-version block
        generatePoSBlocks(self.nodes, 2, 1)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 851):
            raise AssertionFailure("Failed to mine a version=3 blocks")

        # TODO: check that new DERSIG rules are enforced

        # Mine 198 new-version blocks
        generatePoSBlocks(self.nodes, 2, 198)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 1049):
            raise AssertionError("Failed to mine 198 version=3 blocks")

        # Mine 1 old-version block
        generatePoSBlocks(self.nodes, 1, 1)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 1050):
            raise AssertionError("Failed to mine a version=2 block after 949 version=3 blocks")

        # Mine 1 new-version blocks
        generatePoSBlocks(self.nodes, 2, 1)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 1051):
            raise AssertionError("Failed to mine a version=3 block")

        # Mine 1 old-version block
        try:
            generatePoSBlocks(self.nodes, 1, 1)
            raise AssertionError("Succeeded to mine a version=2 block after 950 version=3 blocks")
        except JSONRPCException:
            pass
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 1051):
            raise AssertionError("Accepted a version=2 block after 950 version=3 blocks")

        # Mine 1 new-version block
        generatePoSBlocks(self.nodes, 2, 1)
        self.sync_all()
        if (self.nodes[0].getblockcount() != cnt + 1052):
            raise AssertionError("Failed to mine a version=3 block")

if __name__ == '__main__':
    BIP66Test().main()
