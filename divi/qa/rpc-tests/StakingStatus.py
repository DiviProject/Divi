#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test correct update of staking status when chain tip hasn't been hashed for 5 whole minutes
#
from test_framework import BitcoinTestFramework
from util import *
import time

class StakingStatusTest(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir))

    def run_test(self):
        node = self.nodes[0]
        stats = node.getstakingstatus()
        node.setgenerate(100)
        assert_equal(stats["staking status"],False)
        node.setgenerate(1)
        stats = node.getstakingstatus()
        assert_equal(stats["staking status"],True)
        currentTime = int(time.time())
        node.setmocktime(currentTime + int(5*60))
        stats = node.getstakingstatus()
        assert_equal(stats["staking status"],False)


if __name__ == '__main__':
    StakingStatusTest().main()
