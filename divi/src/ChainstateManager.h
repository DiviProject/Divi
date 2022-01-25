#ifndef CHAINSTATE_MANAGER_H
#define CHAINSTATE_MANAGER_H

#include <atomic>
#include <memory>

class BlockMap;
class CBlockTreeDB;
class CChain;
class CCoinsView;
class CCoinsViewDB;
class CCoinsViewCache;
class CCoinsStats;

/** The main class that encapsulates the blockchain state (including active
 *  chain and the block-index map).  All code that modifies or reads the
 *  blockchain state should do it through an instance of this class.  */
class ChainstateManager
{

private:

  std::unique_ptr<BlockMap> blockMap;
  std::unique_ptr<CChain> activeChain;
  std::unique_ptr<CBlockTreeDB> blockTree;

  std::unique_ptr<CCoinsViewDB> coinsDbView;
  std::unique_ptr<CCoinsView> coinsCatcher;
  std::unique_ptr<CCoinsViewCache> coinsTip;

  /** A refcount for the instance.  We use it to enforce that the
   *  singleton instance is no longer referenced by anything when it
   *  gets destructed.  */
  std::atomic<int> refs;

public:

  class Reference;

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
  const CCoinsViewDB& GetNonCatchingCoinsView () const;

  /** Returns the singleton instance of the ChainstateManager that exists
   *  at the moment.  It must be constructed at the moment.  */
  static ChainstateManager& Get ();

};

/** A reference to the singleton ChainstateManager instance.  This class
 *  is used instead of a raw pointer / reference so that we can track
 *  the existing references and make sure there remain no dangling ones
 *  outside of the singleton's life span.
 *
 *  FIXME: Once we get rid of a "global singleton" in favour of passing
 *  the actual instance explicitly, we should remove this concept.  */
class ChainstateManager::Reference
{

private:

  /** The referred-to instance.  */
  ChainstateManager& instance;

public:

  /** Constructs a new reference that refers to the "global" singleton instance
   *  (which must be alive at the moment).  */
  Reference ();

  ~Reference ();

  inline ChainstateManager*
  operator-> ()
  {
    return &instance;
  }

  inline const ChainstateManager*
  operator-> () const
  {
    return &instance;
  }

  inline ChainstateManager&
  operator* ()
  {
    return instance;
  }

  inline const ChainstateManager&
  operator* () const
  {
    return instance;
  }

};

#endif // CHAINSTATE_MANAGER_H
