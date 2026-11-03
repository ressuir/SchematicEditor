#pragma once
#include "NetModel.h"
#include <vector>
namespace se {
// Document operations are separate from wxWindow so they can be verified without a display.
wxString NextReference(const std::vector<ComponentInstance>& components, const wxString& name);
void AnnotateComponents(std::vector<ComponentInstance>& components);
void AppendCopies(std::vector<ComponentInstance>& components, std::vector<Wire>& wires,
                  const std::vector<ComponentInstance>& copiedComponents,
                  const std::vector<Wire>& copiedWires, const wxPoint& offset);
wxString BuildBom(const std::vector<ComponentInstance>& components);
struct SimulationResult {
    std::vector<int> values; // -1 unknown, 0 low, 1 high, 2 conflicting drivers
    bool converged = false;
    wxString report;
};
// Combinational digital DC evaluation; no analog or timing model.
SimulationResult SimulateDigital(const std::vector<ComponentInstance>& components,
                                 const NetGraph& graph);
}
