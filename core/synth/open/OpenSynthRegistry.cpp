// Open synth engine registry: OpenEngineId -> engine singleton. Adding an engine is one line here plus its
// parameter table in OpenSynthParams.cpp.
#include "open/OpenSynthEngine.h"
#include "open/engines/Acid.h"

namespace opensynth {

ISynthEngine* openSynthEngine(int32_t id) {
    switch (id) {
        case kOpenEngineAcid: return acidEngine();
        default: return nullptr;
    }
}

}  // namespace opensynth
