#pragma once
#include "gbengine/platform/hal.hpp"
namespace GBEngine {
// User-data storage is separate from the immutable original asset filesystem.
class ISaveRepository {
public:
    virtual ~ISaveRepository()=default;
    virtual bool occupied(char slot) const=0;
    virtual Bytes load(char slot) const=0;
    virtual void save(char slot,std::span<const std::uint8_t>)=0;
};
}
