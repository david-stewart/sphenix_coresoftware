/*!
 * \file MicromegasDefs.cc
 * \author Hugo Pereira Da Costa <hugo.pereira-da-costa@cea.fr>
 */

#include "MicromegasDefs.h"

namespace
{
   //* converninece trait for underlying type
  template<class T>
    using underlying_type_t = typename std::underlying_type<T>::type;

  //* convert an strong type enum to integral type
  template<class T>
    constexpr underlying_type_t<T>
    to_underlying_type(T value) noexcept
  { return static_cast<underlying_type_t<T>>(value);}

  /*!
   * hitsetkey layout:
   * Micromegas specific lower 16 bits
   * 24 - 32  tracker id
   * 16 - 24  layer
   * 8 - 16 segmentation type
   * 0 - 8 tile id
   */
  static constexpr unsigned int kBitShiftSegmentation = 8;
  static constexpr unsigned int kBitShiftTileId = 0;

  //! bit shift for hit key
  static constexpr unsigned int kBitShiftStrip = 0;

}

namespace MicromegasDefs
{

  //________________________________________________________________
  TrkrDefs::hitsetkey genHitSetKey(uint8_t layer, SegmentationType type, uint8_t tile )
  {
    TrkrDefs::hitsetkey key = TrkrDefs::genHitSetKey(TrkrDefs::TrkrId::micromegasId, layer);

    TrkrDefs::hitsetkey tmp = to_underlying_type(type);
    key |= (tmp << kBitShiftSegmentation);

    tmp = tile;
    key |= (tmp << kBitShiftTileId);

    return key;
  }

  //________________________________________________________________
  SegmentationType getSegmentationType(TrkrDefs::hitsetkey key)
  {
    TrkrDefs::hitsetkey tmp = (key >> kBitShiftSegmentation);
    return static_cast<SegmentationType>(tmp);
  }

  //________________________________________________________________
  uint8_t getTileId(TrkrDefs::hitsetkey key)
  {
    TrkrDefs::hitsetkey tmp = (key >> kBitShiftTileId);
    return tmp;
  }

  //________________________________________________________________
  TrkrDefs::hitkey genHitKey(uint16_t strip)
  {
    TrkrDefs::hitkey key = strip << kBitShiftStrip;
    return key;
  }

  //________________________________________________________________
  uint16_t getStrip( TrkrDefs::hitkey key )
  {
    TrkrDefs::hitkey tmp = (key >> kBitShiftStrip);
    return tmp;
  }

  //________________________________________________________________
  SegmentationType getSegmentationType(TrkrDefs::cluskey key)
  {
    const TrkrDefs::hitsetkey tmp = TrkrDefs::getHitSetKeyFromClusKey(key);
    return getSegmentationType( tmp );
  }

  //________________________________________________________________
  uint8_t getTileId(TrkrDefs::cluskey key)
  {
    const TrkrDefs::hitsetkey tmp = TrkrDefs::getHitSetKeyFromClusKey(key);
    return getTileId( tmp );
  }

}
