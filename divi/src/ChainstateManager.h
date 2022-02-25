#ifndef CHAINSTATE_MANAGER_H
#define CHAINSTATE_MANAGER_H

#include <memory>

class BlockMap;
class CBlockTreeDB;
class CChain;
class CCoinsView;
class CCoinsViewCache;

/** The main class that encapsulates the blockchain state (including active
 *  chain and the block-index map).  All code that modifies or reads the
 *  blockchain state should do it through an instance of this class.  */
class ChainstateManager
{

private:

  std::unique_ptr<BlockMap> blockMap;
  std::unique_ptr<CChain> activeChain;
  std::unique_ptr<CBlockTreeDB> blockTree;

  std::unique_ptr<CCoinsView> coinsDbView;
  std::unique_ptr<CCoinsView> coinsCatcher;
  std::unique_ptr<CCoinsViewCache> coinsTip;

public:

  explicit ChainstateManager (size_t blockTreeCache, size_t coinDbCache,
                              bool fMemory, bool fWipe);
  ~ChainstateManager ();

  inline BlockMap&
  GetBlockMap ()
  {
    return *blockMap;
  }

  inline const BlockMap&
  GetBlockMap () const
  {
    return *blockMap;
  }

  inline CChain&
  ActiveChain ()
  {
    return *activeChain;
  }

  inline const CChain&
  ActiveChain () const
  {
    return *activeChain;
  }

  inline CBlockTreeDB&
  BlockTree ()
  {
    return *blockTree;
  }

  inline const CBlockTreeDB&
  BlockTree () const
  {
    return *blockTree;
  }

  inline CCoinsViewCache&
  CoinsTip ()
  {
    return *coinsTip;
  }

  inline const CCoinsViewCache&
  CoinsTip () const
  {
    return *coinsTip;
  }

  /** Returns a coins view that is not catching errors in GetCoins.  This is
   *  used during initialisation for verifying the DB.  */
  inline const CCoinsView&
  GetNonCatchingCoinsView () const
  {
    return *coinsDbView;
  }

  /** Returns the singleton instance of the ChainstateManager that exists
   *  at the moment.  It must be constructed at the moment.  */
  static ChainstateManager& Get ();

};

#endif // CHAINSTATE_MANAGER_H
