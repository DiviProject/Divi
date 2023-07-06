#!/usr/bin/env python3
# Copyright (c) 2015 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test ZMQ interface
#

from test_framework import BitcoinTestFramework
from util import *
import zmq

try:
    import http.client as httplib
except ImportError:
    import httplib
try:
    import urllib.parse as urlparse
except ImportError:
    import urlparse

class ZMQTest (BitcoinTestFramework):

    port = 28332

    def setup_nodes(self):
        self.zmqContext = zmq.Context()
        self.zmqSubSocket = self.zmqContext.socket(zmq.SUB)
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashblock")
        self.zmqSubSocket.setsockopt(zmq.SUBSCRIBE, b"hashtx")
        self.zmqSubSocket.connect("tcp://127.0.0.1:%i" % self.port)
        return start_nodes(4, self.options.tmpdir, extra_args=[
            ['-debug=zmq', '-zmqpubhashtx=tcp://127.0.0.1:'+str(self.port), '-zmqpubhashblock=tcp://127.0.0.1:'+str(self.port)],
            [],
            [],
            []
            ])

    def run_test(self):
        self.sync_all()

        genhashes = self.nodes[0].setgenerate( 1)
        self.sync_all()

        print ("listen...")
        #blockhash from generate must be equal to the hash received over zmq
        blockhash_received = False
        while not blockhash_received:
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]
            if(topic == b"hashblock"):
                blkhash = body.hex()
                assert_equal(genhashes[0], blkhash)
                blockhash_received = True
            else:
                time.sleep(0.25)

        assert_equal(blockhash_received,True)


        n = 30
        genhashes = self.nodes[1].setgenerate( n)
        self.sync_all()

        zmqHashes = []
        for x in range(0,n*2):
            msg = self.zmqSubSocket.recv_multipart()
            topic = msg[0]
            body = msg[1]
            if topic == b"hashblock":
                zmqHashes.append(body.hex())

        for x in range(0,n):
            assert_equal(genhashes[x], zmqHashes[x]) #blockhash from generate must be equal to the hash received over zmq

        #test tx from a second node
        hashRPC = self.nodes[1].sendtoaddress(self.nodes[0].getnewaddress(), 1.0)
        self.sync_all()

        # now we should receive a zmq msg because the tx was broadcast
        msg = self.zmqSubSocket.recv_multipart()
        topic = msg[0]
        body = msg[1]
        hashZMQ = ""
        if topic == b"hashtx":
            hashZMQ = body.hex()

        assert_equal(hashRPC, hashZMQ) #tx hash from the RPC must be equal to the hash received over zmq


if __name__ == '__main__':
    ZMQTest ().main ()
