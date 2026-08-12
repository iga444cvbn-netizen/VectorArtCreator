#include "tests/support/workload_builder.h"

#include "core/effects/effect_registry.h"

#include <memory>

namespace vt::test {

Document buildDeterministicWorkload(const WorkloadParameters& parameters)
{
    const int objectCount = qBound(1, parameters.objectCount, 1000);
    const int maskSegments = qBound(0, parameters.maskSegmentsPerObject, 10000);
    const int deformationSamples = qBound(0, parameters.deformationSamplesPerObject, 10000);
    const int generatorCopies = qBound(0, parameters.generatorCopies, 12);
    Document document;
    Page* page = document.currentPage();
    Layer* layer = page->layers.front().get();
    page->id = QStringLiteral("workload-page");
    layer->id = QStringLiteral("workload-layer");
    layer->objects.clear();
    for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex) {
        auto object = std::make_unique<TextObject>();
        object->id = QStringLiteral("workload-object-%1").arg(objectIndex, 4, 10, QLatin1Char('0'));
        object->sourceText = QStringLiteral("Workload %1 АБВГД").arg(objectIndex);
        object->transform.position = QPointF((objectIndex % 10) * 110.0,
                                             (objectIndex / 10) * 80.0);
        if (maskSegments > 0) {
            auto wave = EffectRegistry::instance().create(QStringLiteral("wave"));
            wave->instanceId = QStringLiteral("workload-wave-%1").arg(objectIndex);
            EffectMaskStroke mask;
            mask.radius = 18.0;
            for (int segment = 0; segment <= maskSegments; ++segment) {
                mask.points.push_back(QPointF(segment * 4.0,
                                              objectIndex % 3 + segment * 0.5));
            }
            wave->maskStrokes = {mask};
            object->effects.append(std::move(wave));
        }
        if (generatorCopies > 0) {
            auto echo = EffectRegistry::instance().create(QStringLiteral("echo"));
            echo->instanceId = QStringLiteral("workload-echo-%1").arg(objectIndex);
            static_cast<void>(echo->setParameter(QStringLiteral("copyCount"), generatorCopies));
            object->effects.append(std::move(echo));
        }
        if (deformationSamples > 0) {
            DeformationStroke stroke;
            stroke.radius = 30.0;
            for (int sample = 0; sample < deformationSamples; ++sample) {
                stroke.samples.push_back({QPointF(sample * 2.0, sample * 0.75),
                                          QPointF(0.5, -0.25), 1.0});
            }
            object->deformation.strokes = {stroke};
        }
        layer->objects.push_back(std::move(object));
    }
    document.currentPageId = page->id;
    document.activeLayerId = layer->id;
    document.activeObjectId = layer->objects.front()->id;
    return document;
}

WorkloadSummary inspectWorkload(const Document& document)
{
    WorkloadSummary summary;
    for (const auto& page : document.pages) {
        if (!page) continue;
        for (const auto& layer : page->layers) {
            if (!layer) continue;
            for (const auto& object : layer->objects) {
                if (!object) continue;
                ++summary.objects;
                summary.deformationStrokes += object->deformation.strokes.size();
                for (const DeformationStroke& stroke : object->deformation.strokes) {
                    summary.deformationSamples += stroke.samples.size();
                }
                summary.effects += object->effects.size();
                for (int effectIndex = 0; effectIndex < object->effects.size(); ++effectIndex) {
                    const Effect* effect = object->effects.at(effectIndex);
                    if (!effect) continue;
                    summary.maskStrokes += effect->maskStrokes.size();
                    for (const EffectMaskStroke& mask : effect->maskStrokes) {
                        summary.maskPoints += mask.points.size();
                    }
                    if (effect->generatesGeometry()) {
                        for (const EffectParameter& parameter : effect->parameterDefinitions()) {
                            if (parameter.id == QStringLiteral("copyCount")) {
                                summary.requestedGeneratorCopies += qRound(parameter.value);
                            }
                        }
                    }
                }
            }
        }
    }
    return summary;
}

} // namespace vt::test
