#include "capture/FrameSourceFactory.h"

#ifdef _WIN32
#include "capture/GdiFrameSource.h"
#else
#include "capture/DummyFrameSource.h"
#endif

namespace pilotfly {

std::unique_ptr<IFrameSource> makeFrameSource(const Config& config) {
#ifdef _WIN32
    return std::make_unique<GdiFrameSource>(config.gameWindow);
#else
    (void)config;
    return std::make_unique<DummyFrameSource>();
#endif
}

}
