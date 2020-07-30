#!/usr/bin/env python2
# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

#
# Test sync 
#

from test_framework import BitcoinTestFramework
from bitcoinrpc.authproxy import AuthServiceProxy, JSONRPCException
from util import *
import os
import shutil
import random
from threading import Thread
from Queue import Queue

def mineSingleBlock(miningQueue,nodeId,nblocks):
    while not miningQueue.empty():
        nodeToWorkOn = miningQueue.get()
        try:
            nodeToWorkOn.setgenerate(True,nblocks)
        except Exception as e:
            print('Failed on node id: {0} -- reason: {1}'.format(nodeId,str(e)))
        miningQueue.task_done()
    return True

class SyncTest(BitcoinTestFramework):
    
    def run_test(self):
        # Mine 51 up blocks - by randomly asking nodes
        nodeIdsToGenerateNextBlock = [random.randrange(len(self.nodes)) for j in range(51)]
        numberOfBlocksPerNode = {i: nodeIdsToGenerateNextBlock.count(i) for i in nodeIdsToGenerateNextBlock}

        nodeMiningQueue = Queue(maxsize = 0)
        
        for nodeId in range(len(self.nodes)):
            nodeMiningQueue.put((self.nodes[nodeId]))

        for nodeThreadIndex in range(len(self.nodes)):
            worker = Thread(target=mineSingleBlock,args=[nodeMiningQueue,nodeThreadIndex,numberOfBlocksPerNode[nodeThreadIndex]] )
            worker.setDaemon(True)
            worker.start()
        
        nodeMiningQueue.join()

        self.nodes[1].setgenerate(True, 50)
        bestBlockHash = self.nodes[0].getbestblockhash()
        for node in self.nodes[:1]:
            assert_equal(node.getbestblockhash() , bestBlockHash)

if __name__ == '__main__':
    SyncTest().main()
