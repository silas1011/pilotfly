#include "controller/SinkFactory.h"

#include "controller/DummySink.h"

#ifdef PILOTFLY_HAS_VJOY
#include "controller/VJoySink.h"
#endif

namespace pilotfly {

std::unique_ptr<IControllerSink> makeControllerSink(const Config& config) {
#ifdef PILOTFLY_HAS_VJOY
    return std::make_unique<VJoySink>(config.vjoyDevice);
#else
    (void)config;
    return std::make_unique<DummySink>();
#endif
}

}
