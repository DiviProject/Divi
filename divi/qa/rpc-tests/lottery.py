#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test lottery winners rpc method
#

from test_framework import BitcoinTestFramework
from util import *
from decimal import Decimal

class Lottery (BitcoinTestFramework):

    def setup_network(self):
        self.nodes = []
        self.args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=self.args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=self.args))
        connect_nodes_bi(self.nodes,0,1)
        self.is_network_split=False

    def checkLotteryCandidates(self,node,staking_address,lottery_block_start_height,lottery_cycle=10):

        block_count = node.getblockcount()
        for h in range(100,block_count+1):
            lotteryCandidates = node.getlotteryblockwinners(h)
            if h < lottery_block_start_height or h % lottery_cycle == 0:
                assert_equal(len(lotteryCandidates["Lottery Candidates"]),0)
            elif h >= lottery_block_start_height:
                number_of_candiates = h - lottery_block_start_height
                assert_equal(len(lotteryCandidates["Lottery Candidates"]),number_of_candiates)
                for candidate in lotteryCandidates["Lottery Candidates"]:
                    assert_equal(candidate["Address"],str(staking_address))



    def run_test(self):
        node = self.nodes[0]
        staking_address = self.nodes[1].getnewaddress()
        node.setgenerate(21)
        block_count = node.getblockcount()
        lottery_block_start_height = 101
        target_send = 9999.0
        while block_count < lottery_block_start_height:
            while node.getbalance() > Decimal(target_send):
                node.sendtoaddress(staking_address,target_send)

            blocks_to_mine = min(8,lottery_block_start_height-block_count)
            node.setgenerate( blocks_to_mine )
            block_count = node.getblockcount()
        sync_blocks(self.nodes)
        self.nodes[1].setgenerate(9)
        sync_blocks(self.nodes)
        self.checkLotteryCandidates(node,staking_address,lottery_block_start_height)



if __name__ == '__main__':
    Lottery().main ()
