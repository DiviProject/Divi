#include "StoredMasternodeBroadcasts.h"

namespace
{

/** Magic string for stored broadcasts.  */
constexpr const char* MAGIC_BROADCAST = "mnBroadcast";

/** Wrapper around CMasternodeBroadcast that allows to serialise/unserialise
 *  it and that also implements extra methods as required by the AppendOnlyFile
 *  template code.  */
class MasternodeBroadcastSerializer
{

private:

  CMasternodeBroadcast mnb;

public:

  MasternodeBroadcastSerializer() = default;

  explicit MasternodeBroadcastSerializer(const CMasternodeBroadcast& b)
    : mnb(b)
  {}

  ADD_SERIALIZE_METHODS;

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
  {
    READWRITE(mnb);
  }

  void Clear()
  {
    mnb = CMasternodeBroadcast();
  }

  std::string ToString() const
  {
    return strprintf("CMasternodeBroadcast(%s)", mnb.vin.prevout.ToString());
  }

  const CMasternodeBroadcast& Get() const
  {
    return mnb;
  }

};

} // anonymous namespace

StoredMasternodeBroadcasts::StoredMasternodeBroadcasts(const std::string& file)
  : AppendOnlyFile(file)
{
  auto reader = Read ();
  if (reader != nullptr)
    {
      while (reader->Next())
        {
          const std::string magic = reader->GetMagic();
          if (magic != MAGIC_BROADCAST)
            {
              LogPrint("masternode", "Ignoring chunk '%s' in datafile", magic);
              continue;
            }

          MasternodeBroadcastSerializer mnb;
          if (reader->Parse(mnb))
            broadcasts[mnb.Get().vin.prevout] = mnb.Get();
        }
    }
}

bool StoredMasternodeBroadcasts::AddBroadcast(const CMasternodeBroadcast& mnb)
{
  const MasternodeBroadcastSerializer serializer(mnb);
  if (!Append(MAGIC_BROADCAST, serializer))
    return false;

  broadcasts[serializer.Get().vin.prevout] = serializer.Get();
  return true;
}

bool StoredMasternodeBroadcasts::GetBroadcast(const COutPoint& outp, CMasternodeBroadcast& mnb) const
{
  const auto mit = broadcasts.find(outp);
  if (mit == broadcasts.end())
    return false;

  mnb = mit->second;
  return true;
}
