#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test multisig operation, creation, funding, and withdrawal
#

from test_framework import BitcoinTestFramework
from util import *
from script import *
from decimal import *
import codecs
from messages import *

class MultiSigTest (BitcoinTestFramework):

    def setup_network(self):
        #self.nodes = start_nodes(2, self.options.tmpdir)
        self.nodes = []
        args = ["-debug"]
        self.nodes.append (start_node(0, self.options.tmpdir, extra_args=args))
        self.nodes.append (start_node(1, self.options.tmpdir, extra_args=args))
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def createSharedMultisigAddress(self,sender1,sender2):
        addr1 = sender1.getnewaddress()
        hexPubKey1 = sender1.validateaddress(addr1)["pubkey"]
        addr2 = sender2.getnewaddress()
        hexPubKey2 = sender2.validateaddress(addr2)["pubkey"]

        sharedMultisig = sender1.addmultisigaddress(2,[hexPubKey1,hexPubKey2]) # 2-out-of-2
        sender2.addmultisigaddress(2,[hexPubKey1,hexPubKey2])
        return sharedMultisig

    def createSpendingTransaction(self,multisigTXID,sender1):
        outputs = sender1.getrawtransaction(multisigTXID, 1)["vout"]
        outputIndex = -1
        for output in outputs:
            if output["value"]==Decimal(5000.0):
                outputIndex = output["n"]
                break
        assert(outputIndex > -1)

        claimAddress = sender1.getnewaddress()
        data = sender1.validateaddress (claimAddress)
        scriptToSendTo = codecs.decode (data["scriptPubKey"], "hex")
        amountToSend = int ((Decimal (4999.9)) * COIN)
        tx = CTransaction ()
        tx.vout.append( CTxOut(amountToSend, scriptToSendTo )  )
        tx.vin.append (CTxIn (COutPoint (txid=multisigTXID, n=outputIndex)))

        return tx.serialize ().hex (), amountToSend

    def signMultisig(self,unsigned, sender1, sender2):
        one_sig = sender1.signrawtransaction (unsigned)
        assert_equal(one_sig["complete"],False)
        two_sig = sender2.signrawtransaction (one_sig["hex"])
        assert_equal(two_sig["complete"],True)
        return two_sig

    def createSharedMultisig(self):
        sender1 = self.nodes[0]
        sender2 = self.nodes[1]
        sharedMultisig = self.createSharedMultisigAddress(sender1,sender2)

        # Generate funds for sender2 and fund multisig address
        sender2.setgenerate( 30)
        self.sync_all()
        multisigTXID = sender2.sendtoaddress(sharedMultisig, 5000.0)
        sender2.setgenerate( 1)
        self.sync_all()

        # Create spending transaction paying out of the multisig and into sender1's wallet
        unsigned,amountToSend = self.createSpendingTransaction(multisigTXID,sender1)
        signed_tx = self.signMultisig(unsigned,sender1,sender2)

        # Send out signed tx to the network
        assert_equal(sender1.getbalance(), 0)
        sender1.sendrawtransaction(signed_tx["hex"])
        sync_mempools(self.nodes)
        sender2.setgenerate(1)
        self.sync_all()
        assert_equal(sender1.getbalance(), Decimal(amountToSend)/Decimal(COIN))

    def run_test(self):
        self.createSharedMultisig()


if __name__ == '__main__':
    MultiSigTest().main ()
