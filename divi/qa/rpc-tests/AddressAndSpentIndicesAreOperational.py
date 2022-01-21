#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests indexing flags are functional

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

    def check_utxos(self, staker_node,auditor_node):
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

    def verify_reversal_of_spent_index_under_reorg(self):
        staker_node = self.nodes[0]
        hash = staker_node.setgenerate(1)[0]
        transactions = staker_node.getblock(hash)['tx']
        assert_equal(len(transactions),2)
        coinstake_tx_hash = transactions[1]
        coinstake_tx = staker_node.getrawtransaction(coinstake_tx_hash,1)
        inputs_spent = coinstake_tx['vin']

        parsed_inputs = [ {"txid": input["txid"], "index": input["vout"]} for input in inputs_spent]
        for input in parsed_inputs:
            assert_equal(staker_node.getspentinfo(input)["txid"],coinstake_tx_hash)

        staker_node.invalidateblock(hash)
        for input in parsed_inputs:
            assert_raises(JSONRPCException,staker_node.getspentinfo,input)

        staker_node.reconsiderblock(hash)
        for input in parsed_inputs:
            assert_equal(staker_node.getspentinfo(input)["txid"],coinstake_tx_hash)

    def setup_test(self):
        createPoSStacks ([self.node], self.nodes)
        generatePoSBlocks (self.nodes, 0, 125)
        sync_blocks(self.nodes)

    def run_verify_chain_checks(self):
        self.nodes.append(start_node(1, self.options.tmpdir, extra_args=self.config_args))
        connect_nodes_bi(self.nodes,0,1)
        sync_blocks(self.nodes)
        staker_node = self.nodes[0]
        auditor_node = self.nodes[1]
        print("Verifying chain...")
        auditor_node.verifychain()
        print("Chain verified")
        print("Verifying utxos...")
        self.check_utxos(staker_node,auditor_node)
        print("Finished checking utxos")
        print("Disconnecting chain...")
        hash = auditor_node.getblockhash(110)
        auditor_node.invalidateblock(hash)
        auditor_node.verifychain()
        print("Chain verified")
        print("Reconnecting chain...")
        auditor_node.reconsiderblock(hash)
        auditor_node.verifychain()
        print("Chain verified")

    def run_test (self):
        self.setup_test()
        self.verify_reversal_of_spent_index_under_reorg()
        self.run_verify_chain_checks()


if __name__ == '__main__':
    AddressAndSpentIndicesAreOperational ().main ()
