#pragma once
#include "NetModel.h"
namespace se {
// Split at existing electrical nodes before moving selected component pins.
// Intermediate T nodes remain connected; geometric crossings without a node stay separate.
void MoveConnectedComponents(std::vector<ComponentInstance>& components,std::vector<Wire>& wires,
                             const std::vector<int>& selected,const wxPoint& delta);
}
