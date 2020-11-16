#!/usr/bin/env python3
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
from queue import Queue

def mineSingleBlock(miningQueue):
    while not miningQueue.empty():
        taskObject = miningQueue.get()
        taskCompleted = False
        nodeToWorkOn = taskObject[0]
        blockCount = taskObject[1]
        while not taskCompleted:
            try:
                if blockCount > 0:
                    nodeToWorkOn.setgenerate(True,1)
                    blockCount -= 1
                else:
                    taskCompleted = True
                    miningQueue.task_done()
            except Exception as e:
                # This exception is due to a failure to mine this specific block
                dummyExceptionHandling = str(e)
    return True

class SyncTest(BitcoinTestFramework):

    def run_test(self):
        # Mine 51 up blocks - by randomly asking nodes
        nodeIdsToGenerateNextBlock = [random.randrange(len(self.nodes)) for j in range(51)]
        numberOfBlocksPerNode = {i: nodeIdsToGenerateNextBlock.count(i) for i in nodeIdsToGenerateNextBlock}

        nodeMiningQueues = [ Queue() ] * len(self.nodes)

        for nodeId in range(len(self.nodes)):
            nodeMiningQueues[nodeId].put((self.nodes[nodeId],numberOfBlocksPerNode[nodeId]))

        for nodeThreadIndex in range(len(self.nodes)):
            worker = Thread(target=mineSingleBlock,args=[nodeMiningQueues[nodeThreadIndex]] )
            worker.setDaemon(True)
            worker.start()

        for qObj in nodeMiningQueues:
            qObj.join()

        sync_blocks(self.nodes)
        self.nodes[1].setgenerate(True, 50)
        sync_blocks(self.nodes)
        bestBlockHash = self.nodes[0].getbestblockhash()
        print("Block count totals {}".format(self.nodes[0].getblockcount()) )

        for node in self.nodes[:1]:
            assert_equal(node.getbestblockhash() , bestBlockHash)

if __name__ == '__main__':
    SyncTest().main()
