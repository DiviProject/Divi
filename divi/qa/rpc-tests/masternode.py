# Copyright (c) 2020 The DIVI developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Utilities for testing masternodes.

from test_framework import BitcoinTestFramework
from util import *
import time
from authproxy import JSONRPCException


class MnConfigLine (object):
  """Wrapper class for working with masternode.conf lines."""

  def __init__ (self, line):
    parts = line.split (" ")
    assert_equal (len (parts), 5)

    self.line = line

    self.alias = parts[0]
    self.ip = parts[1]
    self.privkey = parts[2]
    self.txid = parts[3]
    self.vout = int (parts[4])


class MnSetupData (object):
  """Wrapper class for working with setting up remote masternodes"""
  def __init__(self, setup_data,address=None):
    assert_equal(len(setup_data), 4)

    self.protocol_version = setup_data["protocol_version"]
    self.message_to_sign = setup_data["message_to_sign"]
    self.cfg = MnConfigLine(setup_data["config_line"])
    self.broadcast_data = setup_data["broadcast_data"]
    if address is not None:
      self.address = address

def setup_masternode(mempoolSync,controlNode, hostNode,alias,tier,hostIP, utxo = None):
  """Calls setup_masternode with the given data and returns the
  MnConfigLine instance."""
  txdata = None
  sync_required = True
  if utxo is None:
    txdata = controlNode.allocatefunds("masternode",alias,tier)
  else:
    sync_required = False
    txdata=utxo
  data_from_tx = controlNode.getrawtransaction(txdata["txhash"],1)["vout"][txdata["vout"]]
  address = data_from_tx["scriptPubKey"]["addresses"]
  assert_equal(len(address),1)
  address = address[0]
  pubkey = controlNode.validateaddress(address)["pubkey"]
  if sync_required:
    mempoolSync()
  data = hostNode.setupmasternode(alias,txdata["txhash"],str(txdata["vout"]), pubkey, hostIP)
  return MnSetupData (data,address)

class MnTestFramework(BitcoinTestFramework):
  def __init__ (self):
    super ().__init__ ()
    self.base_args = ["-debug=masternode", "-debug=mocktime"]
    self.setup = []
    self.mn_control_node_indices = {}
    self.mn_host_node_indices = {}
    self.number_of_nodes = 7

  def setup_chain (self):
    super().setup_chain(number_of_nodes=self.number_of_nodes)

  def for_each_masternode(self, masternode_function):
    assert_equal(len(self.nodes),len(self.setup))
    result = []
    for nodeIndex in range(len(self.nodes)):
      if self.setup[nodeIndex] is not None:
        result.append(masternode_function(nodeIndex))

    return result

  def setup_masternode(self,controlNodeIndex, hostNodeIndex,alias,tier,utxo=None):
    def mempoolSync():
      sync_mempools (self.nodes)
    controlNode = self.nodes[controlNodeIndex]
    hostNode = self.nodes[hostNodeIndex]
    self.setup[hostNodeIndex] = setup_masternode(mempoolSync,controlNode,hostNode,alias, tier,"localhost:%d" % p2p_port (hostNodeIndex),utxo=utxo)
    self.mn_control_node_indices[alias] = controlNodeIndex
    self.mn_host_node_indices[alias] = hostNodeIndex

  def broadcast_with_ping_update(self,alias):
    controlNode = self.nodes[self.mn_control_node_indices[alias]]
    hostNodeSetup = self.setup[self.mn_host_node_indices[alias]]
    hostNode = self.nodes[self.mn_host_node_indices[alias]]
    signed_broadcast = controlNode.signmnbroadcast(str(hostNodeSetup.broadcast_data))["broadcast_data"]
    return hostNode.broadcaststartmasternode(signed_broadcast,"update_ping")

  def broadcast_start(self, alias, broadcastLocal,sig=None):
    controlNode = self.nodes[self.mn_control_node_indices[alias]]
    hostNodeSetup = self.setup[self.mn_host_node_indices[alias]]
    if sig is None:
      sig = controlNode.signmessage(hostNodeSetup.address, hostNodeSetup.message_to_sign , "hex", "hex")
    if broadcastLocal:
      return controlNode.broadcaststartmasternode(str(hostNodeSetup.broadcast_data),str(sig))
    else:
      hostNode = self.nodes[self.mn_host_node_indices[alias]]
      return hostNode.broadcaststartmasternode(str(hostNodeSetup.broadcast_data),str(sig))

  def check_masternodes_are_locally_active(self):
    # Check status of the masternodes themselves.
    def check_masternode_status(nodeIndex):
      data = self.nodes[nodeIndex].getmasternodestatus ()
      config = self.setup[nodeIndex].cfg
      assert_equal (data["status"], 4)
      assert_equal (data["txhash"], config.txid)
      assert_equal (data["outputidx"],config.vout)
      assert_equal (data["message"], "Masternode successfully started")
    self.for_each_masternode(check_masternode_status)

  def check_masternodes_are_locally_inactive(self):
    # Check status of the masternodes themselves.
    def check_masternode_status(nodeIndex):
      assert_raises(JSONRPCException, self.nodes[nodeIndex].getmasternodestatus)
    self.for_each_masternode(check_masternode_status)

  def wait_for_masternodes_to_be_locally_active(self,updateMockTime=False):
    # Check status of the masternodes themselves.
    def check_masternode_status(nodeIndex):
      try:
        data = self.nodes[nodeIndex].getmasternodestatus ()
        config = self.setup[nodeIndex].cfg
        assert_equal (data["status"], 4)
        assert_equal (data["txhash"], config.txid)
        assert_equal (data["outputidx"],config.vout)
        assert_equal (data["message"], "Masternode successfully started")
        return True
      except:
        if updateMockTime:
          for _ in range(100):
            self.time += 1
            set_node_times(self.nodes,self.time)
            time.sleep(0.01)
        else:
          time.sleep(0.1)
        return False
    while True:
      if all(self.for_each_masternode(check_masternode_status)):
        break

  def wait_for_mnsync_on_nodes (self, updateMockTime = False):
    print ("Running masternode sync...")
    for node in self.nodes:
      while True:
        if node is None:
          break
        status = node.mnsync ("status")
        if str(status["currentMasternodeSyncStatus"])!=str(999):
          print("...")
          for _ in range(100):
            if updateMockTime:
              self.time += 1
              set_node_times(self.nodes,self.time)
            time.sleep(.01)
        else:
          break

  def stop_masternode_daemons(self):
    def stop_masternode(nodeIndex):
      if self.nodes[nodeIndex] is not None:
        stop_node(self.nodes[nodeIndex],nodeIndex)
        self.nodes[nodeIndex] = None
      else:
        assert_equal("WARNING -- trying to stop already stopped connection","")
    self.for_each_masternode(stop_masternode)

  def start_masternode_daemons(self,updateMockTime=False):
    def start_masternode(nodeIndex):
      if self.nodes[nodeIndex] is not None:
        assert_equal("WARNING -- overwriting connection","")
      else:
        args = self.base_args[:]
        conf = self.setup[nodeIndex].cfg
        args.append ("-masternode=%s" % conf.alias)
        self.nodes[nodeIndex] = start_node (nodeIndex, self.options.tmpdir, extra_args=args, mn_config_lines=[conf.line])
        if updateMockTime:
          set_node_times([self.nodes[nodeIndex]],self.time)
    self.for_each_masternode(start_masternode)

  def wait_for_mn_list_to_sync(self,node_to_wait_on,expected_mn_count):
    print("Waiting for mn list to sync...")
    mn_list = node_to_wait_on.listmasternodes()
    attemps = 10
    while len(mn_list) < expected_mn_count:
      self.time += 1
      set_node_times(self.nodes,self.time)
      mn_list = node_to_wait_on.listmasternodes()
      attemps -= 1
      time.sleep(0.01)
      if attemps < 0:
        attemps = 10
        node_to_wait_on.mnsync("reset")
    assert_equal (len (mn_list), expected_mn_count)
    return mn_list

  def check_list_for_all_masternodes(self,mn_list):
    def find_mn(nodeIndex):
      mn_node = self.nodes[nodeIndex]
      local_status = mn_node.getmasternodestatus()
      node_found = False
      for listedMN in mn_list:
        if local_status["addr"] == listedMN["addr"]:
          assert_equal(listedMN["status"],"ENABLED")
          assert_equal (local_status["status"], 4)
          assert_equal(local_status["txhash"],listedMN["txhash"])
          assert_equal(local_status["outputidx"],listedMN["outidx"])
          node_found = True
      assert_equal(node_found,True)
    self.for_each_masternode(find_mn)

  def connect_masternodes_to_peers(self,peersByIndex,updateMockTime=False):
    def connect_masternode(nodeIndex):
      for i in peersByIndex:
          connect_nodes_bi(self.nodes, nodeIndex, i)
          if updateMockTime:
            self.time +=1
            set_node_times(self.nodes,self.time)
    self.for_each_masternode(connect_masternode)

  def mock_wait(self,duration=1):
    for _ in range(duration):
      self.time +=1
      set_node_times(self.nodes,self.time)
      time.sleep(0.01)