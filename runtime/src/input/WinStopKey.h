#pragma once

#include "app/Config.h"
#include "input/IStopKey.h"

namespace pilotfly {

class WinStopKey : public IStopKey {
public:
    explicit WinStopKey(const StopKey& key);

    std::string keyName() const override;
    bool consumePress() override;
    bool usableFromAnyThread() const override;

private:
    StopKey key_;
    int virtualKey_;
    bool wasDown_ = false;
};

}
