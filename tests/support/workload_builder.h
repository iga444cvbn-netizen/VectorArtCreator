#pragma once

#include "core/document/document.h"

namespace vt::test {

struct WorkloadParameters {
    int objectCount = 1;
    int maskSegmentsPerObject = 0;
    int deformationSamplesPerObject = 0;
    int generatorCopies = 0;
};

struct WorkloadSummary {
    int objects = 0;
    int effects = 0;
    int maskStrokes = 0;
    int maskPoints = 0;
    int deformationStrokes = 0;
    int deformationSamples = 0;
    int requestedGeneratorCopies = 0;
};

[[nodiscard]] Document buildDeterministicWorkload(const WorkloadParameters& parameters);
[[nodiscard]] WorkloadSummary inspectWorkload(const Document& document);

} // namespace vt::test
