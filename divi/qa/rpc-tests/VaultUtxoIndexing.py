#!/usr/bin/env python3
# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests detection of vault transactions in spent and address indexing

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *
from PowToPosTransition import createPoSStacks, generatePoSBlocks

import codecs
from decimal import Decimal
import random

class VaultUtxoIndexing (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.config_args = ["-debug","-spentindex=1","-addressindex=1"]
        staker_config_args = self.config_args + ["-vault=1"]
        self.nodes = start_nodes (3, self.options.tmpdir, extra_args=[self.config_args,staker_config_args,self.config_args])
        connect_nodes_bi(self.nodes,0,1)
        connect_nodes_bi(self.nodes,0,2)
        self.owner = self.nodes[0]
        self.staker = self.nodes[1]
        self.monitor = self.nodes[2]
        self.is_network_split = False

    def create_vault_stacks(self):
        self.owner.setgenerate(50)
        self.owner_vault_addresses = []
        self.vault_data = []
        print("Creating vault utxos...")
        for _ in range(3):
            owner_node_address = self.owner.getnewaddress()
            vault_node_address = self.staker.getnewaddress()
            vault_encoding = owner_node_address+":"+vault_node_address
            vaulting_result = self.owner.fundvault(vault_encoding,10000.0)

            print("Node balance: {} | New vault: {}".format(self.owner.getbalance(),vaulting_result))
            self.vault_data.append(vaulting_result)
            self.owner_vault_addresses.append(owner_node_address)

        self.owner.setgenerate(50)
        self.vault_hashes = []
        for vault_datum in self.vault_data:
            assert self.staker.addvault(vault_datum["vault"][0]["encoding"],vault_datum["txhash"])["succeeded"]
            self.vault_hashes.append(vault_datum["txhash"])
        sync_blocks(self.nodes)

    def check_vaults_deposits_are_indexed(self):
        # Check that vault utxos are found when querying by address
        for addr in self.owner_vault_addresses:
            utxos = self.owner.getaddressutxos(addr, True)
            assert_equal(len(utxos),1)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},True)["balance"],10000*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},False)["balance"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},False)["received"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]})["balance"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]})["received"],0*COIN)

            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)},True)
            assert_equal(balance_updates[0]["satoshis"], 10000*COIN)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)},False)
            assert_equal(len(balance_updates), 0)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)})
            assert_equal(len(balance_updates), 0)

            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(52),"end":int(100)},True)
            assert_equal( len(balance_updates), 0)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(52),"end":int(100)},False)
            assert_equal( len(balance_updates), 0)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(52),"end":int(100)})
            assert_equal( len(balance_updates), 0)

        found_txids = self.monitor.getaddresstxids({"addresses":self.owner_vault_addresses},True)
        for hash in self.vault_hashes:
            assert hash in found_txids
        assert_equal(len(found_txids),len(self.vault_hashes))


    def check_vault_stakes_are_indexed(self):
        for addr in self.owner_vault_addresses:
            self.staker.setgenerate(1)

        staking_reward = 456*COIN
        for addr in self.owner_vault_addresses:
            sync_blocks(self.nodes)
            utxos = self.owner.getaddressutxos(addr, True)
            assert len(utxos) >= 1
            assert_greater_than(self.monitor.getaddressbalance({"addresses":[addr]},True)["balance"],10000*COIN + staking_reward - 1)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},False)["balance"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},False)["received"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]})["balance"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]})["received"],0*COIN)

            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)},True)
            assert_equal(len(balance_updates), 1)
            assert_equal(balance_updates[0]["satoshis"], 10000*COIN)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)},False)
            assert_equal(len(balance_updates), 0)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(52)})
            assert_equal(len(balance_updates), 0)

            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(53),"end":int(103)},True)
            assert_equal(len(balance_updates), 2)
            total_delta = sum([delta["satoshis"] for delta in balance_updates])
            assert_greater_than(total_delta, staking_reward - 1)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(53),"end":int(103)},False)
            assert_equal(len(balance_updates), 0)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(53),"end":int(103)})
            assert_equal(len(balance_updates), 0)

    def check_non_vault_utxos_do_not_register_as_vaults(self):
        all_utxos = self.owner.listunspent()
        all_txids = [utxo["txid"] for utxo in all_utxos]
        non_vault_utxos = []
        non_vault_addresses = []
        for utxo in all_utxos:
            txid = utxo["txid"]
            vout = utxo["vout"]
            txdata = self.owner.getrawtransaction(txid,1)
            vault_detected = False
            output = txdata["vout"][vout]
            if output["scriptPubKey"]["type"] != str("vault"):
                non_vault_utxos.append(utxo)
                non_vault_addresses.append(output["scriptPubKey"]["addresses"][0])
                break
            else:
                assert False

        for addr in non_vault_addresses:
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},True)["balance"],0*COIN)
            assert_equal(self.monitor.getaddressbalance({"addresses":[addr]},True)["received"],0*COIN)
            balance_updates = self.monitor.getaddressdeltas({"addresses":[addr],"start": int(1),"end":int(200)},True)
            assert_equal(len(balance_updates),0)
            vault_utxos = self.monitor.getaddressutxos(addr,True)
            assert_equal(len(vault_utxos),0)




    def run_test (self):
        self.create_vault_stacks()

        # Check that vault utxos are found when querying by address
        self.check_vaults_deposits_are_indexed()
        self.check_non_vault_utxos_do_not_register_as_vaults()
        self.check_vault_stakes_are_indexed()
        self.check_non_vault_utxos_do_not_register_as_vaults()
        self.owner.setgenerate(20)
        sync_blocks(self.nodes)
        self.check_non_vault_utxos_do_not_register_as_vaults()
        self.check_vault_stakes_are_indexed()



if __name__ == '__main__':
    VaultUtxoIndexing ().main ()
