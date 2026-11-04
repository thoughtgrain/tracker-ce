// Open synth engine registry: OpenEngineId -> engine singleton. Adding an engine is one line here plus its
// parameter table in OpenSynthParams.cpp.
#include "open/OpenSynthEngine.h"
#include "open/engines/Acid.h"
#include "open/engines/Toro.h"

namespace opensynth {

ISynthEngine* openSynthEngine(int32_t id) {
    switch (id) {
        case kOpenEngineAcid: return acidEngine();
        case kOpenEngineToro: return toroEngine();
        default: return nullptr;
    }
}

}  // namespace opensynth
