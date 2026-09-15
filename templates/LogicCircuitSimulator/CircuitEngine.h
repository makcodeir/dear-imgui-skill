#pragma once
#include "CircuitData.h"

namespace LogicSim {

class LogicEngine {
public:
    // Performs logic simulation step over the circuit graph.
    // Handles clock tick accumulation and iterative propagation across gate depths.
    static void Step(CircuitGraph& graph, float deltaTime, int maxPropagationIterations = 16);

    // Helper to decode binary inputs (4-bit BCD or 4 pins) to 7-segment active segments (bits 0..6 -> a..g)
    // Segments layout:
    //      a
    //    f   b
    //      g
    //    e   c
    //      d
    static uint8_t BCDTo7Segment(uint8_t value);
};

} // namespace LogicSim
