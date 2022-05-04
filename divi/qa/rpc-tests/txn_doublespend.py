#!/usr/bin/env python3
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test proper accounting with malleable transactions
#

from test_framework import BitcoinTestFramework
from authproxy import AuthServiceProxy, JSONRPCException
from decimal import Decimal
from util import *
import os
import shutil

class TxnMallTest(BitcoinTestFramework):

    def add_options(self, parser):
        parser.add_option("--mineblock", dest="mine_block", default=False, action="store_true",
                          help="Test double-spend of 1-confirmed transaction")

    def setup_nodes(self):
        args = [["-spendzeroconfchange","-relaypriority=0"]] * 4
        return start_nodes(4, self.options.tmpdir, extra_args=args)

    def collect_utxos_by_account(self, node, account_names = [],confirmations=1):
        account_utxos = {}
        if len(account_names) == 0:
            return account_utxos
        for name in account_names:
            account_utxos[name] = []
        utxos = node.listunspent(confirmations)
        for utxo in utxos:
            if "account" in utxo and utxo["account"] in account_names:
                account_utxos[utxo["account"]].append(utxo)

        return account_utxos

    def collect_balances_by_account(self, node, account_names = [],confirmations=0):
        account_utxos = self.collect_utxos_by_account(node,account_names,confirmations)
        account_balances = {}
        for account in account_utxos:
            account_balances[account] = 0
            for utxo in account_utxos[account]:
                account_balances[account] += utxo["amount"]

        return account_balances

    def create_doublespend_for_later(self,sender, node1_address):
        utxos_by_account = self.collect_utxos_by_account(sender,["foo","bar"])
        inputs = []
        for utxo in utxos_by_account["foo"]:
            inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"]} )

        outputs = {}
        outputs[self.foo_address] = 9.999
        outputs[node1_address] = 1210.0
        rawtx = sender.createrawtransaction(inputs, outputs)
        doublespend = sender.signrawtransaction(rawtx)
        assert_equal(doublespend["complete"], True)
        return doublespend

    def create_daisy_chain_transactions(self,sender,node1_address):
        utxos_by_account = self.collect_utxos_by_account(sender,["foo","bar"])
        inputs = []
        for utxo in utxos_by_account["foo"]:
            inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"], "address" : utxo["address"] } )
        for utxo in utxos_by_account["bar"]:
            inputs.append({ "txid" : utxo["txid"], "vout" : utxo["vout"], "address" : utxo["address"] } )

        outputs = {}
        outputs[self.bar_address] = 29.999
        outputs[self.foo_address] = 9.999
        outputs[node1_address] = 1210

        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx1 = self.nodes[0].signrawtransaction(rawtx)
        txid1=self.nodes[0].sendrawtransaction(tx1["hex"])

        inputs = []
        inputs.append({ "txid" : txid1, "vout" : 0 } )

        outputs = {}
        outputs[self.bar_address] = 9.999
        outputs[node1_address] = 19.999
        rawtx = self.nodes[0].createrawtransaction(inputs, outputs)
        tx2 = self.nodes[0].signrawtransaction(rawtx)
        txid2=self.nodes[0].sendrawtransaction(tx2["hex"])

        return txid1, txid2

    def run_test(self):
        # Mine blocks to give nodes 0-2 each 1'250 coins.
        # We use node 3 to mine blocks, and don't need it to have
        # a specific balance later on.
        self.nodes[3].setgenerate( 5)
        self.sync_all()
        for i in range(3):
            self.nodes[i].setgenerate( 1)
            self.sync_all()
        self.nodes[3].setgenerate( 20)
        self.sync_all()

        # All nodes should start with 1,250 BTC:
        starting_balance = 1250
        for i in range(3):
            assert_equal(self.nodes[i].getbalance(), starting_balance)
            self.nodes[i].getnewaddress("")  # bug workaround, coins generated assigned to first getnewaddress!

        # Assign coins to foo and bar accounts:
        sender = self.nodes[0]
        to = {}
        foo_address = sender.getnewaddress("foo")
        bar_address = sender.getnewaddress("bar")
        self.foo_address = foo_address
        self.bar_address = bar_address
        to[foo_address] = 1219.9995
        to[bar_address] = 29.9995
        txid0 = sender.sendmany("", to)
        self.sync_all()
        self.nodes[1].setgenerate(1)
        self.sync_all()
        tx0 = sender.gettransaction(txid0)
        assert_equal(tx0["confirmations"],1)
        assert_near(sender.getbalance(""),starting_balance-Decimal(to[foo_address]+to[bar_address]), Decimal(0.001))

        # Split the network for now.
        self.split_network()

        # Coins are sent to node1_address
        node1_address = self.nodes[1].getnewaddress("from0")

        # First: use raw transaction API to send 1210 DIVI to node1_address,
        # but don't broadcast:
        doublespend = self.create_doublespend_for_later(sender,node1_address)

        # Create two transaction from node[0] to node[1]; the
        # second must spend change from the first because the first
        # spends all mature inputs:
        txid1,txid2 = self.create_daisy_chain_transactions(sender,node1_address)

        # Have node0 mine a block:
        if self.options.mine_block:
            self.nodes[0].setgenerate( 1)
            self.sync_all()

        tx1 = self.nodes[0].gettransaction(txid1)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Node0's balance should be starting balance minus 1210,
        # minus 20, and minus transaction fees:
        node0_balance_change = (tx1["amount"] + tx2["amount"])
        fees_paid = tx1["fee"] + tx2["fee"]
        expected = starting_balance + node0_balance_change
        amount_sent = -node0_balance_change - fees_paid

        wallet_info = self.nodes[0].getwalletinfo()
        total_confirmed_and_unconfirmed_balance = wallet_info["balance"] + wallet_info["unconfirmed_balance"]
        assert_near(total_confirmed_and_unconfirmed_balance, expected, Decimal(0.001))

        # foo and bar accounts should be debited:
        account_balances = self.collect_balances_by_account(sender,account_names=["foo","bar"])
        assert_near(account_balances["foo"], 1220+tx1["amount"],Decimal(0.001))
        assert_near(account_balances["bar"], 30+tx2["amount"],Decimal(0.001))

        if self.options.mine_block:
            assert_equal(tx1["confirmations"], 1)
            assert_equal(tx2["confirmations"], 1)
            # Node1's "from0" balance should be both transaction amounts:
            assert_equal(self.nodes[1].getbalance("from0"), amount_sent)
        else:
            assert_equal(tx1["confirmations"], 0)
            assert_equal(tx2["confirmations"], 0)

        # Now give doublespend to miner:
        mutated_txid = self.nodes[2].sendrawtransaction(doublespend["hex"])
        # ... mine some blocks...
        blks = self.nodes[2].setgenerate( 5)

        # Reconnect the split network, and sync chain:
        connect_nodes(self.nodes[1], 2)
        self.nodes[2].setgenerate( 1)  # Mine another block to make sure we sync
        sync_blocks(self.nodes)

        # Re-fetch transaction info:
        tx1 = self.nodes[0].gettransaction(txid1)
        tx2 = self.nodes[0].gettransaction(txid2)

        # Both transactions should be conflicted
        assert_equal(tx1["confirmations"], -1)
        assert_equal(tx2["confirmations"], -1)

        # Node0's total balance should be starting balance minus
        # 1210 for the double-spend:
        expected = starting_balance - 1210
        assert_near(self.nodes[0].getbalance(), expected, Decimal(0.001))
        assert_near(self.nodes[0].getbalance("*"), expected, Decimal(0.001))

        # foo account should be debited, but bar account should not:
        account_balances = self.collect_balances_by_account(sender,account_names=["foo","bar"])
        assert_near(account_balances["foo"], 10, Decimal(0.001))
        assert_near(account_balances["bar"], 30, Decimal(0.001))

        # Node1's "from" account balance should be just the mutated send:
        assert_near(self.nodes[1].getbalance("from0"), 1210, Decimal(0.001))

if __name__ == '__main__':
    TxnMallTest().main()
