#pragma once

#include <string>

namespace pilotfly {

class IStopKey {
public:
    virtual ~IStopKey() = default;
    virtual std::string keyName() const = 0;
    virtual bool consumePress() = 0;
    virtual bool usableFromAnyThread() const = 0;
};

}
