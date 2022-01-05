#!/usr/bin/env python3
# Copyright (c) 2021 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Tests execution of HTLCs (which are one side of atomic trades).

from test_framework import BitcoinTestFramework
from authproxy import JSONRPCException
from messages import *
from util import *
from script import *

from CheckLockTimeVerify import ACTIVATION_TIME


class AtomicTradingTest (BitcoinTestFramework):

    def setup_network (self, split=False):
        self.nodes = start_nodes (2, self.options.tmpdir)
        connect_nodes (self.nodes[0], 1)
        self.is_network_split = False

    def generateOutput (self, n, addr, amount):
        """Sends amount DIVI to the given address, and returns
        a (txid, n) pair corresponding to the created output."""

        txid = n.sendtoaddress (addr, amount)

        tx = n.getrawtransaction (txid, 1)
        for i in range (len (tx["vout"])):
          if tx["vout"][i]["scriptPubKey"]["addresses"] == [addr]:
            return (txid, i)

        raise AssertionError ("failed to find destination address")

    def buildSpendingTx(self,node,output,address,lockTime =0):
        (txid, n) = output
        outpoint = COutPoint (txid=txid, n=n)

        tx = CTransaction ()
        tx.nLockTime = lockTime
        tx.vin.append (CTxIn (outpoint))
        tx.vout.append (CTxOut (1, CScript ([OP_META])))

        prevout = node.gettxout (txid, n)
        prevtx = [{
          "txid": txid,
          "vout": n,
          "scriptPubKey": prevout["scriptPubKey"]["hex"],
          "redeemScript": self.htlc.hex (),
        }]

        privkeys = [node.dumpprivkey (address)]
        signedTx = FromHex(CTransaction(), node.signrawtransaction (ToHex (tx), prevtx, privkeys)["hex"])
        return signedTx

    def appendSecretToSpendingTx(self,signedTx,secrets = {}):
        for inputIndex in secrets:
            scriptSig = [x for x in CScript(signedTx.vin[inputIndex].scriptSig)]
            scriptSig[2] = secrets[inputIndex]
            signedTx.vin[inputIndex].scriptSig = CScript(scriptSig)
        return signedTx

    def buildSpend (self, node, output, address, secret, lockTime=0):
        """Builds a transaction that spends the given output.  It uses
        the passed secret in the scriptSig, and the pubkey/signature
        associated to the address to sign with the given node (which
        must know the private key to the address)."""

        signed = self.buildSpendingTx(node,output,address,lockTime)
        # It is not necessarily the case that signed["complete"] is true
        # here, as the solver does not have the secret preimage.  It will
        # add the right signatures, though, so then we just need to fill
        # in the secret.
        tx = self.appendSecretToSpendingTx(signed,{0:secret})
        return tx

    def expectInvalid (self, tx):
        """Expects that a given transaction is invalid."""
        assert_raises (JSONRPCException, self.nodes[0].generateblock,
                       {"extratx": [ToHex (tx)]})

    def expectValidAndMine (self, tx):
        """Expects that a given transaction is valid."""
        self.nodes[0].generateblock({"extratx": [ToHex (tx)]})

    def run_test (self):
        set_node_times (self.nodes, ACTIVATION_TIME)
        connect_nodes (self.nodes[0], 1)
        self.nodes[0].setgenerate ( 30)

        # We need two addresses from our two nodes, and mainly
        # also the raw pubkey hashes.
        addr = [n.getnewaddress () for n in self.nodes]
        p2pkh = [
            self.nodes[0].validateaddress (a)["scriptPubKey"]
            for a in addr
        ]
        baseScripts = [
            CScript (binascii.unhexlify (h))
            for h in p2pkh
        ]
        pkh = [
            [x for x in b][2]
            for b in baseScripts
        ]

        # Define a secret preimage and its hash value.
        secret = b"foobar"
        secretHash = hashlib.sha256 (secret).digest ()

        # We lock two coins in two identical outputs (two instances
        # of our HTLC), so that we can test spending them both
        # according to the relevant paths.
        #
        # The output can be spent with a signature of addr[0] after
        # a time lock (i.e. after block 100), or with a SHA-256
        # preimage by addr[1] immediately.
        #
        # The scriptSig will be "sig pubkey preimage" for both spends,
        # where preimage can be an arbitrary value (not matching the hash)
        # for spending with addr[0] after the time lock.
        self.htlc = CScript ([
            OP_SHA256, secretHash, OP_EQUAL,
            OP_IF,
              pkh[1],
            OP_ELSE,
              100, OP_CHECKLOCKTIMEVERIFY, OP_DROP, pkh[0],
            OP_ENDIF,
            OP_OVER, OP_HASH160, OP_EQUALVERIFY, OP_CHECKSIG,
        ])
        htlcAddress = self.nodes[0].decodescript (self.htlc.hex ())["p2sh"]
        outputs = [
            self.generateOutput (self.nodes[0], htlcAddress, 1)
            for _ in range (2)
        ]

        connect_nodes_bi(self.nodes,0,1)
        self.nodes[0].setgenerate ( 1)
        sync_blocks (self.nodes)

        # addr[1] can spend the output immediately with the right secret.
        tx = self.buildSpend (self.nodes[1], outputs[0], addr[1], b"wrong")
        self.expectInvalid (tx)
        tx = self.buildSpend (self.nodes[1], outputs[0], addr[1], secret)
        self.nodes[0].sendrawtransaction (ToHex (tx))
        self.nodes[0].setgenerate ( 1)

        # addr[0] can't spend before the time lock runs out, independent
        # of the secret.
        tx = self.buildSpend (self.nodes[0], outputs[1], addr[0], b"wrong")
        self.expectInvalid (tx)
        tx = self.buildSpend (self.nodes[0], outputs[1], addr[0], secret)
        self.expectInvalid (tx)
        tx = self.buildSpend (self.nodes[0], outputs[1], addr[0], b"wrong",
                              lockTime=100)
        self.expectInvalid (tx)

        # Bump the block height to after the time lock.  Now addr[0] can
        # spend the output (with a wrong secret, since otherwise
        # the codepath for addr[1] activates).
        numberOfBlocks = 99 - self.nodes[0].getblockcount()
        self.nodes[0].setgenerate ( numberOfBlocks)
        sync_blocks (self.nodes)
        tx = self.buildSpend (self.nodes[0], outputs[1], addr[0], b"wrong",
                              lockTime=100)
        self.expectInvalid(tx)
        self.nodes[0].setgenerate ( 1)
        self.expectValidAndMine(tx)

        # Both outputs should now really be spent.
        for txid, n in outputs:
          assert_equal (self.nodes[0].gettxout (txid, n), None)


if __name__ == '__main__':
    AtomicTradingTest ().main ()
