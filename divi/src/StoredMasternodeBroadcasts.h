#ifndef STORED_MASTERNODE_BROADCASTS_H
#define STORED_MASTERNODE_BROADCASTS_H

#include "flat-database.h"
#include "masternode.h"
#include "primitives/transaction.h"

#include <map>

/** Helper class that keeps track of pre-signed and stored masternode
 *  broadcasts this node knows about.  They are stored on-disk in an
 *  append-only data file.  */
class StoredMasternodeBroadcasts : private AppendOnlyFile
{

private:

  /** In-memory map of all the broadcasts (loaded from the file).  */
  std::map<COutPoint, CMasternodeBroadcast> broadcasts;

public:

  /** Constructs the instance based on the given data file.  */
  explicit StoredMasternodeBroadcasts(const std::string& file);

  /** Imports a new broadcast, which is stored in memory and written
   *  to the on-disk file.  */
  bool AddBroadcast(const CMasternodeBroadcast& mnb);

  /** Tries to look up a broadcast by outpoint.  Returns true and fills in
   *  the broadcast if found, and false if not.  */
  bool GetBroadcast(const COutPoint& outp, CMasternodeBroadcast& mnb) const;

  /** Returns the entire map of stored broadcasts.  */
  const std::map<COutPoint, CMasternodeBroadcast>& GetMap() const
  {
    return broadcasts;
  }

};

#endif // STORED_MASTERNODE_BROADCASTS_H
