#ifndef CHAINSTATE_MANAGER_H
#define CHAINSTATE_MANAGER_H

class BlockMap;
class CBlockTreeDB;
class CChain;
class CCoinsViewCache;

/** The main class that encapsulates the blockchain state (including active
 *  chain and the block-index map).  All code that modifies or reads the
 *  blockchain state should do it through an instance of this class.  */
class ChainstateManager
{

private:

  BlockMap& blockMap;
  CChain& activeChain;
  CBlockTreeDB& blockTree;
  CCoinsViewCache& coinsTip;

public:

  /** Constructs a fresh instance that refers to the globals.
   *
   *  TODO: Remove the globals and instead make the instances held
   *  by the ChainstateManager instance.  Until then, the ChainstateManager
   *  is a singleton, and only one instance must be created at a time.  */
  ChainstateManager ();

  ~ChainstateManager ();

  inline BlockMap&
  GetBlockMap ()
  {
    return blockMap;
  }

  inline const BlockMap&
  GetBlockMap () const
  {
    return blockMap;
  }

  inline CChain&
  ActiveChain ()
  {
    return activeChain;
  }

  inline const CChain&
  ActiveChain () const
  {
    return activeChain;
  }

  inline CBlockTreeDB&
  BlockTree ()
  {
    return blockTree;
  }

  inline const CBlockTreeDB&
  BlockTree () const
  {
    return blockTree;
  }

  inline CCoinsViewCache&
  CoinsTip ()
  {
    return coinsTip;
  }

  inline const CCoinsViewCache&
  CoinsTip () const
  {
    return coinsTip;
  }

  /** Returns the singleton instance of the ChainstateManager that exists
   *  at the moment.  It must be constructed at the moment.  */
  static ChainstateManager& Get ();

};

#endif // CHAINSTATE_MANAGER_H
