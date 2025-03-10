#include "SvtxVertex_v2.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>  // for swap

SvtxVertex_v2::SvtxVertex_v2()
{
  std::fill(std::begin(_pos), std::end(_pos), std::numeric_limits<float>::quiet_NaN());
  std::fill(std::begin(_err), std::end(_err), std::numeric_limits<float>::quiet_NaN());
}

void SvtxVertex_v2::identify(std::ostream& os) const
{
  os << "---SvtxVertex_v2--------------------" << std::endl;
  os << "vertexid: " << get_id() << std::endl;

  os << " t0 = " << get_t() << std::endl;
  os << " beam crossing = " << get_beam_crossing() << std::endl;
  os << " (x,y,z) =  (" << get_position(0);
  os << ", " << get_position(1) << ", ";
  os << get_position(2) << ") cm" << std::endl;

  os << " chisq = " << get_chisq() << ", ";
  os << " ndof = " << get_ndof() << std::endl;

  os << "         ( ";
  os << get_error(0, 0) << " , ";
  os << get_error(0, 1) << " , ";
  os << get_error(0, 2) << " )" << std::endl;
  os << "  err  = ( ";
  os << get_error(1, 0) << " , ";
  os << get_error(1, 1) << " , ";
  os << get_error(1, 2) << " )" << std::endl;
  os << "         ( ";
  os << get_error(2, 0) << " , ";
  os << get_error(2, 1) << " , ";
  os << get_error(2, 2) << " )" << std::endl;

  os << " list of tracks ids: ";
  for (ConstTrackIter iter = begin_tracks(); iter != end_tracks(); ++iter)
  {
    os << *iter << " ";
  }
  os << std::endl;
  os << "-----------------------------------------------" << std::endl;

  return;
}

int SvtxVertex_v2::isValid() const
{
  if (_id == std::numeric_limits<unsigned int>::max())
  {
    return 0;
  }
  if (std::isnan(_t0))
  {
    return 0;
  }
  if (std::isnan(_chisq))
  {
    return 0;
  }
  if (_ndof == std::numeric_limits<unsigned int>::max())
  {
    return 0;
  }

  for (float _po : _pos)
  {
    if (std::isnan(_po))
    {
      return 0;
    }
  }
  for (int j = 0; j < 3; ++j)
  {
    for (int i = j; i < 3; ++i)
    {
      if (std::isnan(get_error(i, j)))
      {
        return 0;
      }
    }
  }
  if (_track_ids.empty())
  {
    return 0;
  }
  return 1;
}

void SvtxVertex_v2::set_error(unsigned int i, unsigned int j, float value)
{
  _err[covar_index(i, j)] = value;
  return;
}

float SvtxVertex_v2::get_error(unsigned int i, unsigned int j) const
{
  return _err[covar_index(i, j)];
}

unsigned int SvtxVertex_v2::covar_index(unsigned int i, unsigned int j) const
{
  if (i > j)
  {
    std::swap(i, j);
  }
  return i + 1 + (j + 1) * (j) / 2 - 1;
}
