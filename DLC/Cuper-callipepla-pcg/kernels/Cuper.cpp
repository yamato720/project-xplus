#include "Cuper.h"

// Isolated Callipepla-style full-PCG experiment.
//
// Matrix format and SpMV datapath come from DLC/Cuper-jacobi-iteration's
// strip16 Cuper service.  The PCG state/update side is new for this directory
// and follows the Callipepla reference at docs/refer/callipepla_pcg_reference
// at the task-graph/phase level without importing Callipepla's sparse format.
#include "detail/pcg_callipepla_top_graphs.hpp"
