#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test correct update of staking status when chain tip hasn't been hashed for 5 whole minutes
#
from test_framework import BitcoinTestFramework
from util import *
import string
import random

class StakingWithLockedCoins(BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.is_network_split = False
        self.nodes.append(start_node(0, self.options.tmpdir))

    def lock_an_unknown_single_coins(self, node):
        all_utxos = node.listunspent()
        utxos_with_less_than_25_confs = [utxo for utxo in all_utxos if utxo["confirmations"] < 25]
        assert_greater_than(len(utxos_with_less_than_25_confs),0)
        hexcharacters = string.ascii_lowercase[0:6] + ''.join([str(i) for i in range(10)])
        print(hexcharacters)
        utxo_description = {
            "txid": ''.join(random.choice(hexcharacters) for i in range(64)),
            "vout": 0,
        }
        node.lockunspent(False, [utxo_description])

    def run_test(self):
        node = self.nodes[0]
        stats = node.getstakingstatus()
        node.setgenerate(100)
        assert_equal(stats["staking status"],False)
        self.lock_an_unknown_single_coins(node)
        node.setgenerate(1)
        stats = node.getstakingstatus()
        assert_equal(stats["staking status"],True)


if __name__ == '__main__':
    StakingWithLockedCoins().main()
