#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test sending funds and balance computation for wallets
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

    def createSharedMultisigAddress(self,sender1,sender2):
        addr1 = sender1.getnewaddress()
        hexPubKey1 = sender1.validateaddress(addr1)["pubkey"]
        addr2 = sender2.getnewaddress()
        hexPubKey2 = sender2.validateaddress(addr2)["pubkey"]

        sharedMultisig = sender1.addmultisigaddress(2,[hexPubKey1,hexPubKey2]) # 2-out-of-2
        sender2.addmultisigaddress(2,[hexPubKey1,hexPubKey2])
        return sharedMultisig

    def getUTXOReference(self,multisigTXID,sender1):
        outputs = sender1.getrawtransaction(multisigTXID, 1)["vout"]
        outputIndex = -1
        for output in outputs:
            if output["value"]==Decimal(5000.0):
                outputIndex = output["n"]
                break
        assert(outputIndex > -1)

        return {"txid":multisigTXID, "n":outputIndex}

    def signAllInputs(self,unsigned, sender1, sender2):
        one_sig = sender1.signrawtransaction (unsigned)
        two_sig = sender2.signrawtransaction (one_sig["hex"])
        return two_sig

    def ensure_complex_spends_resolve_balances_correctly(self,accountName=""):
        sender = self.sender
        receiver = self.receiver
        sender.setgenerate(20)
        self.sync_all()

        starting_balance = receiver.getbalance()
        reference_balance = receiver.getbalance()
        alt_balance = receiver.getbalance("*")
        acc_balance = receiver.getbalance(accountName,1, True)
        print("Before: {} | {} | {} | {}".format(starting_balance,reference_balance,alt_balance,acc_balance))

        sharedMultisig = self.createSharedMultisigAddress(sender,receiver)
        multisigTxID = sender.sendtoaddress(sharedMultisig,5000.0)
        multisigOutputIndex = self.getUTXOReference(multisigTxID,sender)["n"]
        multisigScript = codecs.decode(sender.validateaddress(sharedMultisig)["scriptPubKey"],"hex")

        # Without these the account balance can appear negative
        receiver.setaccount(sharedMultisig,accountName)
        receiver.importaddress(sharedMultisig,accountName)

        addr = receiver.getnewaddress()
        txid = sender.sendtoaddress(addr,5000.0)
        tx = sender.getrawtransaction (txid, 1)
        receiver_utxo = []
        for i in range (len (tx["vout"])):
          if tx["vout"][i]["scriptPubKey"]["addresses"] == [addr]:
            receiver_utxo = (txid, i)

        self.sync_all()
        sender.setgenerate(1)
        self.sync_all()

        reference_balance = receiver.getbalance()
        alt_balance = receiver.getbalance("*")
        acc_balance = receiver.getbalance(accountName,1, True)
        print("Middle: {} | {} | {} | {}".format(starting_balance,reference_balance,alt_balance,acc_balance))

        sender_utxo = sender.listunspent()[0]
        tx = CTransaction ()
        tx.vin.append (CTxIn (COutPoint (txid=sender_utxo["txid"], n=sender_utxo["vout"])))
        tx.vin.append (CTxIn (COutPoint (txid=receiver_utxo[0], n=receiver_utxo[1])))
        tx.vin.append (CTxIn (COutPoint (txid=multisigTxID, n=multisigOutputIndex)))

        #amountToSend = int ((Decimal (2500.0) - Decimal ('0.1')) * COIN)
        addr = receiver.getnewaddress()
        data = receiver.validateaddress (addr)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        amountToSend = int ((Decimal (90.1) - Decimal ('0.1')) * COIN)
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )

        addr = sender.getnewaddress()
        data = sender.validateaddress (addr)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        amountToSend = int ((Decimal (800.1) - Decimal ('0.1')) * COIN)
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )

        amountToSend = int ((Decimal (7000.1) - Decimal ('0.1')) * COIN)
        tx.vout.append( CTxOut(amountToSend, multisigScript )  )

        signed = self.signAllInputs(tx.serialize().hex(),sender,receiver)
        sender.sendrawtransaction(signed["hex"], True)
        sender.setgenerate( 1)
        self.sync_all()

        reference_balance = receiver.getbalance()
        alt_balance = receiver.getbalance("*")
        acc_balance = receiver.getbalance(accountName,1, True)
        print("After: {} | {} | {} | {}".format(starting_balance,reference_balance,alt_balance,acc_balance))


    def ensure_change_output_only_for_positive_change_amounts(self):
        sender = self.sender
        receiver = self.receiver
        addr = receiver.getnewaddress()
        sender.setgenerate( 30)
        self.sync_all()
        sender.sendtoaddress(addr, 5000.0)
        sender.setgenerate( 1)
        self.sync_all()
        receiver.sendtoaddress(sender.getnewaddress(),receiver.getbalance()-Decimal(0.499950) )
        self.sync_all()
        sender.setgenerate(1)
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

        sender.setgenerate(1)
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
        sender.setgenerate( 1)
        self.sync_all()
        reference_balance = receiver.getbalance()
        alt_balance = receiver.getbalance("*")
        acc_balance = receiver.getbalance("new_account")
        assert_equal(reference_balance,alt_balance)
        assert_equal(reference_balance, acc_balance + starting_balance)

    def send_to_many_with_repetitions(self):
        sender_wallet_info = self.sender.getwalletinfo()
        balance_before = sender_wallet_info["balance"]+sender_wallet_info["unconfirmed_balance"]
        addr1 = self.receiver.getnewaddress()
        addr2 = self.receiver.getnewaddress()
        addr3 = self.receiver.getnewaddress()
        sendmany_format = {}
        sendmany_format[addr1] = {"amount":100.0,"repetitions": 7}
        sendmany_format[addr2] = {"amount":100.0,"repetitions": 9}
        sendmany_format[addr3] = 100.0*5
        amount_sent = Decimal(100.0*(7 + 9 + 5))
        txid = self.sender.sendmany("",sendmany_format)
        sender_wallet_info = self.sender.getwalletinfo()
        balance_after = sender_wallet_info["balance"]+sender_wallet_info["unconfirmed_balance"]
        assert_near(balance_before,balance_after+amount_sent,Decimal(1e-3))

    def run_test(self):
        self.ensure_change_output_only_for_positive_change_amounts()
        self.join_sends_compute_balance_correctly()
        self.ensure_complex_spends_resolve_balances_correctly()
        self.ensure_complex_spends_resolve_balances_correctly("safekeeping")
        self.send_to_many_with_repetitions()

if __name__ == '__main__':
    WalletSends().main ()
