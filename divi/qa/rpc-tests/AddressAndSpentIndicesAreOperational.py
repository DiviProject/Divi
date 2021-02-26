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
import random

class AddressAndSpentIndicesAreOperational (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.config_args = ["-debug","-spentindex=1","-addressindex=1"]
        self.nodes = start_nodes (1, self.options.tmpdir, extra_args=[self.config_args])
        self.node = self.nodes[0]
        self.is_network_split = False

    def restart_single_node_to_verify_index_database(self):
        stop_node (self.nodes[1], 1)
        self.nodes[1] = None
        self.nodes[1] = start_node(1, self.options.tmpdir, extra_args=self.config_args)
        time.sleep(1.0)
        reconnect_all(self.nodes)
        sync_blocks(self.nodes)

    def run_test (self):
        createPoSStacks ([self.node], self.nodes)
        generatePoSBlocks (self.nodes, 0, 125)
        sync_blocks(self.nodes)

        self.nodes.append(start_node(1, self.options.tmpdir, extra_args=self.config_args))
        connect_nodes_bi(self.nodes,0,1)
        sync_blocks(self.nodes)

        staker_node = self.nodes[0]
        auditor_node = self.nodes[1]
        utxos = staker_node.listunspent()
        assert_greater_than(len(utxos), 0)
        assert_equal(staker_node.getbestblockhash(), auditor_node.getbestblockhash())
        staker_addresses = set()
        utxo_count_by_address = {}
        for utxo in utxos:
            addr = utxo["address"]
            staker_addresses.add(addr)
            utxo_count_by_address[addr] = utxo_count_by_address.get(addr,0) + 1

        random_addresses = random.sample([x for x in staker_addresses],5)
        recovered_utxo_count = 0
        expected_utxo_count = sum( [ utxo_count_by_address[addr] for addr in random_addresses ] )
        for addr in random_addresses:
            query_results = auditor_node.getaddressutxos(addr)
            assert_greater_than(len(query_results),0)
            recovered_utxo_count += len(query_results)
            for utxo in query_results:
                txraw = auditor_node.getrawtransaction(utxo["txid"])
                tx = auditor_node.decoderawtransaction(txraw)
                for input in tx["vin"]:
                    if "coinbase" in input:
                        continue
                    spent_input_json={"txid":input["txid"], "index":input["vout"]}
                    auditor_node.getspentinfo(spent_input_json)
        assert_equal(recovered_utxo_count, expected_utxo_count)
        self.restart_single_node_to_verify_index_database()

if __name__ == '__main__':
    AddressAndSpentIndicesAreOperational ().main ()
