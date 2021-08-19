#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test ZMQ interface
#

from test_framework import BitcoinTestFramework
from util import *
from messages import *
from script import *
import codecs
from decimal import Decimal
class WalletSends (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False
        self.sender = self.nodes[0]
        self.receiver = self.nodes[1]

    def ensure_change_output_only_for_positive_change_amounts(self):
        sender = self.sender
        receiver = self.receiver
        addr = receiver.getnewaddress()
        sender.setgenerate(True, 30)
        self.sync_all()
        sender.sendtoaddress(addr, 5000.0)
        sender.setgenerate(True, 1)
        self.sync_all()
        receiver.sendtoaddress(sender.getnewaddress(),receiver.getbalance()-Decimal(0.499950) )
        self.sync_all()
        sender.setgenerate(True,1)
        self.sync_all()

    def join_sends_compute_balance_correctly(self):
        sender = self.sender
        receiver = self.receiver
        starting_balance = receiver.getbalance()
        addr = receiver.getnewaddress("new_account")
        txid = sender.sendtoaddress(addr,5000.0)
        tx = sender.getrawtransaction (txid, 1)
        receiver_utxo = []
        for i in range (len (tx["vout"])):
          if tx["vout"][i]["scriptPubKey"]["addresses"] == [addr]:
            receiver_utxo = (txid, i)

        sender.setgenerate(True,1)
        self.sync_all()

        sender_utxo = sender.listunspent()[0]
        tx = CTransaction ()
        tx.vin.append (CTxIn (COutPoint (txid=sender_utxo["txid"], n=sender_utxo["vout"])))
        tx.vin.append (CTxIn (COutPoint (txid=receiver_utxo[0], n=receiver_utxo[1])))

        amountToSend = int ((Decimal (2500.0) - Decimal ('0.1')) * COIN)
        addr = receiver.getnewaddress("new_account")
        data = receiver.validateaddress (addr)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )

        addr = sender.getnewaddress()
        data = sender.validateaddress (addr)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )

        unsigned = tx.serialize ().hex ()
        sig1 = sender.signrawtransaction(unsigned)
        sig2 = receiver.signrawtransaction(sig1["hex"])

        sender.sendrawtransaction(sig2["hex"], True)
        sender.setgenerate(True, 1)
        self.sync_all()
        reference_balance = receiver.getbalance()
        alt_balance = receiver.getbalance("*")
        acc_balance = receiver.getbalance("new_account")
        assert_equal(reference_balance,alt_balance)
        assert_equal(reference_balance, acc_balance + starting_balance)



    def run_test(self):
        self.ensure_change_output_only_for_positive_change_amounts()
        self.join_sends_compute_balance_correctly()


if __name__ == '__main__':
    WalletSends().main ()
