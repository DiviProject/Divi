#!/usr/bin/env python3
# Copyright (c) 2023 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests OP_LIMIT_TRANSFER opcode and fork activation.

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *

ACTIVATION_TIME = 2_000_000_000


class CheckLimitTransferVerifyTest (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.nodes = start_nodes (2, self.options.tmpdir,[["--mocktime="+str(ACTIVATION_TIME+1_000)]]*2)
        self.node = self.nodes[0]
        self.is_network_split = False
        connect_nodes(self.nodes[0],1)

    def generateOutput (self, addr, amount):
        """Sends amount DIVI to the given address, and returns
        a (txid, n) pair corresponding to the created output."""

        txid = self.node.sendtoaddress (addr, amount)

        tx = self.node.getrawtransaction (txid, 1)
        for i in range (len (tx["vout"])):
          if tx["vout"][i]["scriptPubKey"]["addresses"] == [addr]:
            return (txid, i)

        raise AssertionError ("failed to find destination address")

    def buildSpend (self, output, scriptSig, destinations = [ { "amount": 1, "script": CScript ([OP_META]) } ] ):
        """Builds a transaction that spends one of our test outputs."""

        (txid, n) = output
        outpoint = COutPoint (txid=txid, n=n)
        tx = CTransaction ()
        tx.vin.append (CTxIn (outpoint, scriptSig))
        for destination in destinations:
            tx.vout.append (CTxOut (destination["amount"], destination["script"]))

        return tx

    def run_test(self):
        self.run_test_b()

    def run_test_b (self):
        # Generate outputs locked to block height 100, but spendable
        # easily (by pushing 42 on the stack) afterwards.
        self.node.setgenerate (30)
        sync_blocks(self.nodes)
        changeAddressDetails = self.node.decodescript(CScript([42, OP_EQUAL]).hex())
        changeAddressScript = CScript(
            bytearray.fromhex( self.node.validateaddress(changeAddressDetails["p2sh"])["scriptPubKey"])
        )
        p2shID = hash160( bytearray.fromhex(CScript([42, OP_EQUAL]).hex()) )#[::-1]
        amountTransferLimitInSats = 10*COIN # 10 DIVI

        #Change address 8vSkjqmyxE32xcjYG4TtmdR3tUwuoS2QU2 Subscription address 8hm9AfkNZu516J3zKZkLkkzKTvZiaC4yzT
        limitTransferP2SHScript = CScript ([CScriptNum(amountTransferLimitInSats), p2shID, OP_LIMIT_TRANSFER ])
        limitTransferScriptP2SH = self.node.decodescript(limitTransferP2SHScript.hex())["p2sh"]

        assert_equal(changeAddressDetails["p2sh"], "8vSkjqmyxE32xcjYG4TtmdR3tUwuoS2QU2")

        output = self.generateOutput(limitTransferScriptP2SH, 100.0)

        sync_mempools(self.nodes)

        self.node.setgenerate(1)
        sync_blocks(self.nodes)

        # Malicious spending
        receiver = self.nodes[1]
        addr = receiver.getnewaddress()
        addrDetails = receiver.validateaddress(addr)

        receiverDestination = CScript(
            bytearray.fromhex(addrDetails["scriptPubKey"])
        )

        scriptSig = CScript([OP_TRUE, bytearray.fromhex(limitTransferP2SHScript.hex())])
        maliciousSpends = []
        # Exceeds limit and does not return change amount
        maliciousSpends.append(
            self.buildSpend(
                output,
                scriptSig,
                destinations = [{"amount": 20*COIN, "script": receiverDestination}])
        )
        # Does not exceed limit but does not return change amount
        maliciousSpends.append(
            self.buildSpend(
                output,
                scriptSig,
                destinations = [{"amount": 9*COIN, "script": receiverDestination}])
        )
        # Does not exceed limit and returns change amount, but in the incorrect output index
        maliciousSpends.append(
            self.buildSpend(
                output,
                scriptSig,
                destinations = [
                    {"amount": 9*COIN, "script": receiverDestination},
                    {"amount": 90*COIN, "script": changeAddressScript}
                ]
            )
        )

        for spendingTx in maliciousSpends:
            assert_raises (JSONRPCException, self.node.sendrawtransaction, spendingTx.serialize().hex())
            assert_raises (JSONRPCException, self.node.generateblock, {"extratx": [spendingTx.serialize ().hex ()]})

        sync_blocks(self.nodes)
        self.node.setgenerate(20)
        sync_blocks(self.nodes)

        validSpending = self.buildSpend(
            output,
            scriptSig,
            destinations = [
                {"amount":91*COIN, "script": changeAddressScript},
                {"amount": 8*COIN + 10*CENT, "script": receiverDestination},
            ]
        )
        self.node.generateblock({"extratx": [ validSpending.serialize ().hex ()  ]})
        sync_blocks(self.nodes)
        assert_near(receiver.getbalance(), Decimal(8*COIN + 10*CENT)/Decimal(COIN), Decimal(0.01))

if __name__ == '__main__':
    CheckLimitTransferVerifyTest ().main ()
