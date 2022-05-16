#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test sending funds and balance computation for wallets
#

from random import Random
from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from util import *
from messages import *
from script import *
import codecs
from decimal import Decimal
class SendMoneyOptions (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug","-spendzeroconfchange"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(2, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        connect_nodes (self.nodes[0], 2)
        connect_nodes (self.nodes[1], 2)
        self.is_network_split = False
        self.sender = self.nodes[0]
        self.receiver = self.nodes[1]
        self.minter = self.nodes[2]

    def run_test(self):
        self.sender.setgenerate(5)
        self.sync_all()
        self.minter.setgenerate(25)
        self.sync_all()
        balance_before = self.sender.getbalance()
        initial_balance = balance_before
        assert_equal(balance_before,1250*5)
        amount_to_send = Decimal( str(round(float(balance_before)*0.95*random.random(),4)) )
        self.sender.sendtoaddress(self.receiver.getnewaddress(),amount_to_send,"receiver_pays")
        sync_mempools(self.nodes)
        self.minter.setgenerate(1)
        self.sync_all()
        remaining_balance = self.sender.getbalance()
        print("RECEIVER_PAYS_FOR_TX_FEES mode:\n\tStarted with {}, sent {}, remaining with {}".format(initial_balance,amount_to_send,remaining_balance))
        assert_equal(remaining_balance,initial_balance-amount_to_send)
        receiver_balance = self.receiver.getbalance()
        assert_greater_than(amount_to_send, receiver_balance)
        assert_greater_than(receiver_balance+Decimal(1.0),amount_to_send)
        sender_remaining_balance = self.sender.getbalance()
        send_less_than_all = sender_remaining_balance - Decimal(1.0)
        self.sender.sendtoaddress(self.receiver.getnewaddress(),send_less_than_all,"sweep_funds")
        sync_mempools(self.nodes)
        self.minter.setgenerate(1)
        self.sync_all()
        remaining_balance_after_sweep_funds = self.sender.getbalance()
        print("SWEEP_FUNDS mode:\n\tStarted with {}, sent {}, remaining with {}".format(sender_remaining_balance, send_less_than_all, remaining_balance_after_sweep_funds))
        assert_equal(self.sender.getbalance(),0)
        assert_greater_than(self.receiver.getbalance()+Decimal(1.0), amount_to_send + sender_remaining_balance)
        self.minter.sendtoaddress(self.sender.getnewaddress(),1000.0)
        sync_mempools(self.nodes)
        self.minter.setgenerate(1)
        self.sync_all()
        receiver_address = self.receiver.getnewaddress()
        assert_raises(JSONRPCException,self.sender.sendtoaddress,receiver_address,1000.0,"receiver_pays")
        self.sender.sendtoaddress(receiver_address, 1000.0, "sweep_funds")
        sync_mempools(self.nodes)
        self.minter.setgenerate(1)
        self.sync_all()



if __name__ == '__main__':
    SendMoneyOptions().main ()
