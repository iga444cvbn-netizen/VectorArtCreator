#include "tests/support/semantic_equality.h"

namespace vt::test {
namespace {

template <typename T>
bool equalValue(const T& expected, const T& actual, const QString& path, QString* difference)
{
    if (expected == actual) return true;
    if (difference) {
        *difference = QStringLiteral("semantic mismatch at %1").arg(path);
    }
    return false;
}

bool compareMasks(const QVector<EffectMaskStroke>& expected,
                  const QVector<EffectMaskStroke>& actual,
                  const QString& path, QString* difference)
{
    if (!equalValue(expected.size(), actual.size(), path + QStringLiteral(".size"), difference)) return false;
    for (int index = 0; index < expected.size(); ++index) {
        const auto& left = expected.at(index);
        const auto& right = actual.at(index);
        const QString prefix = path + QStringLiteral("[%1]").arg(index);
        if (!equalValue(left.points, right.points, prefix + QStringLiteral(".points"), difference)
            || !equalValue(left.radius, right.radius, prefix + QStringLiteral(".radius"), difference)
            || !equalValue(left.opacity, right.opacity, prefix + QStringLiteral(".opacity"), difference)
            || !equalValue(left.hardness, right.hardness, prefix + QStringLiteral(".hardness"), difference)
            || !equalValue(left.restore, right.restore, prefix + QStringLiteral(".restore"), difference)) {
            return false;
        }
    }
    return true;
}

bool compareDeformation(const ManualDeformation& expected, const ManualDeformation& actual,
                        const QString& path, QString* difference)
{
    if (!equalValue(expected.enabled, actual.enabled, path + QStringLiteral(".enabled"), difference)
        || !equalValue(expected.strength, actual.strength, path + QStringLiteral(".strength"), difference)
        || !equalValue(expected.strokes.size(), actual.strokes.size(), path + QStringLiteral(".strokes.size"), difference)) {
        return false;
    }
    for (int index = 0; index < expected.strokes.size(); ++index) {
        const auto& left = expected.strokes.at(index);
        const auto& right = actual.strokes.at(index);
        const QString prefix = path + QStringLiteral(".strokes[%1]").arg(index);
        if (!equalValue(left.mode, right.mode, prefix + QStringLiteral(".mode"), difference)
            || !equalValue(left.target, right.target, prefix + QStringLiteral(".target"), difference)
            || !equalValue(left.radius, right.radius, prefix + QStringLiteral(".radius"), difference)
            || !equalValue(left.strength, right.strength, prefix + QStringLiteral(".strength"), difference)
            || !equalValue(left.hardness, right.hardness, prefix + QStringLiteral(".hardness"), difference)
            || !equalValue(left.coordinateSpace, right.coordinateSpace,
                           prefix + QStringLiteral(".coordinateSpace"), difference)
            || !equalValue(left.samples, right.samples, prefix + QStringLiteral(".samples"), difference)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool compareEffects(const Effect& expected, const Effect& actual,
                    QString* difference, const QString& path)
{
    if (!equalValue(expected.typeId(), actual.typeId(), path + QStringLiteral(".typeId"), difference)
        || !equalValue(expected.instanceId, actual.instanceId, path + QStringLiteral(".instanceId"), difference)
        || !equalValue(expected.enabled, actual.enabled, path + QStringLiteral(".enabled"), difference)
        || !equalValue(expected.masterStrength, actual.masterStrength,
                       path + QStringLiteral(".masterStrength"), difference)
        || !equalValue(expected.scope.kind, actual.scope.kind, path + QStringLiteral(".scope.kind"), difference)
        || !equalValue(expected.scope.start, actual.scope.start, path + QStringLiteral(".scope.start"), difference)
        || !equalValue(expected.scope.end, actual.scope.end, path + QStringLiteral(".scope.end"), difference)
        || !compareMasks(expected.maskStrokes, actual.maskStrokes,
                         path + QStringLiteral(".maskStrokes"), difference)
        || !equalValue(expected.maskInverted, actual.maskInverted,
                       path + QStringLiteral(".maskInverted"), difference)
        || !equalValue(expected.generatesGeometry(), actual.generatesGeometry(),
                       path + QStringLiteral(".generatesGeometry"), difference)
        || !equalValue(expected.parametersToJson(), actual.parametersToJson(),
                       path + QStringLiteral(".parameters"), difference)) {
        return false;
    }
    const auto expectedDefinitions = expected.parameterDefinitions();
    const auto actualDefinitions = actual.parameterDefinitions();
    if (!equalValue(expectedDefinitions.size(), actualDefinitions.size(),
                    path + QStringLiteral(".parameterDefinitions.size"), difference)) return false;
    for (int index = 0; index < expectedDefinitions.size(); ++index) {
        const auto& left = expectedDefinitions.at(index);
        const auto& right = actualDefinitions.at(index);
        const QString prefix = path + QStringLiteral(".parameterDefinitions[%1]").arg(index);
        if (!equalValue(left.id, right.id, prefix + QStringLiteral(".id"), difference)
            || !equalValue(left.value, right.value, prefix + QStringLiteral(".value"), difference)
            || !equalValue(left.minimum, right.minimum, prefix + QStringLiteral(".minimum"), difference)
            || !equalValue(left.maximum, right.maximum, prefix + QStringLiteral(".maximum"), difference)
            || !equalValue(left.step, right.step, prefix + QStringLiteral(".step"), difference)
            || !equalValue(left.integer, right.integer, prefix + QStringLiteral(".integer"), difference)) {
            return false;
        }
    }
    return true;
}

bool compareTextObjects(const TextObject& expected, const TextObject& actual,
                        QString* difference, const QString& path)
{
    if (!equalValue(expected.id, actual.id, path + QStringLiteral(".id"), difference)
        || !equalValue(expected.sourceText, actual.sourceText, path + QStringLiteral(".sourceText"), difference)
        || !equalValue(expected.font, actual.font, path + QStringLiteral(".font"), difference)
        || !equalValue(expected.typography.fontSize, actual.typography.fontSize,
                       path + QStringLiteral(".typography.fontSize"), difference)
        || !equalValue(expected.typography.trackingEm, actual.typography.trackingEm,
                       path + QStringLiteral(".typography.trackingEm"), difference)
        || !equalValue(expected.typography.lineSpacing, actual.typography.lineSpacing,
                       path + QStringLiteral(".typography.lineSpacing"), difference)
        || !equalValue(expected.fill, actual.fill, path + QStringLiteral(".fill"), difference)
        || !equalValue(expected.effectStackStrength, actual.effectStackStrength,
                       path + QStringLiteral(".effectStackStrength"), difference)
        || !compareDeformation(expected.deformation, actual.deformation,
                                path + QStringLiteral(".deformation"), difference)
        || !equalValue(expected.path, actual.path,
                       path + QStringLiteral(".path"), difference)
        || !equalValue(expected.pathLayout, actual.pathLayout,
                       path + QStringLiteral(".pathLayout"), difference)
        || !equalValue(expected.layoutMode, actual.layoutMode,
                       path + QStringLiteral(".layoutMode"), difference)
        || !equalValue(expected.region, actual.region,
                       path + QStringLiteral(".region"), difference)
        || !equalValue(expected.regionLayout, actual.regionLayout,
                       path + QStringLiteral(".regionLayout"), difference)
        || !equalValue(expected.transform.position, actual.transform.position,
                       path + QStringLiteral(".transform.position"), difference)
        || !equalValue(expected.transform.rotation, actual.transform.rotation,
                       path + QStringLiteral(".transform.rotation"), difference)
        || !equalValue(expected.transform.scale, actual.transform.scale,
                       path + QStringLiteral(".transform.scale"), difference)
        || !equalValue(expected.transform.pivotLocal, actual.transform.pivotLocal,
                       path + QStringLiteral(".transform.pivotLocal"), difference)
        || !equalValue(expected.transform.hasPivot, actual.transform.hasPivot,
                       path + QStringLiteral(".transform.hasPivot"), difference)
        || !equalValue(expected.visible, actual.visible, path + QStringLiteral(".visible"), difference)
        || !equalValue(expected.futureData, actual.futureData, path + QStringLiteral(".futureData"), difference)
        || !equalValue(expected.effects.size(), actual.effects.size(),
                       path + QStringLiteral(".effects.size"), difference)) {
        return false;
    }
    for (int index = 0; index < expected.effects.size(); ++index) {
        const Effect* left = expected.effects.at(index);
        const Effect* right = actual.effects.at(index);
        if (!left || !right) {
            if (left != right && difference) {
                *difference = QStringLiteral("semantic mismatch at %1.effects[%2]").arg(path).arg(index);
            }
            if (left != right) return false;
            continue;
        }
        if (!compareEffects(*left, *right, difference,
                            path + QStringLiteral(".effects[%1]").arg(index))) return false;
    }
    return true;
}

bool compareLayers(const Layer& expected, const Layer& actual,
                   QString* difference, const QString& path)
{
    if (!equalValue(expected.id, actual.id, path + QStringLiteral(".id"), difference)
        || !equalValue(expected.name, actual.name, path + QStringLiteral(".name"), difference)
        || !equalValue(expected.visible, actual.visible, path + QStringLiteral(".visible"), difference)
        || !equalValue(expected.locked, actual.locked, path + QStringLiteral(".locked"), difference)
        || !equalValue(expected.objects.size(), actual.objects.size(),
                       path + QStringLiteral(".objects.size"), difference)) return false;
    for (size_t index = 0; index < expected.objects.size(); ++index) {
        const auto& left = expected.objects[index];
        const auto& right = actual.objects[index];
        if (!left || !right) {
            if (left != right && difference) {
                *difference = QStringLiteral("semantic mismatch at %1.objects[%2]")
                                  .arg(path).arg(qulonglong(index));
            }
            if (left != right) return false;
            continue;
        }
        if (!compareTextObjects(*left, *right, difference,
                                path + QStringLiteral(".objects[%1]").arg(qulonglong(index)))) return false;
    }
    return true;
}

bool comparePages(const Page& expected, const Page& actual,
                  QString* difference, const QString& path)
{
    if (!equalValue(expected.id, actual.id, path + QStringLiteral(".id"), difference)
        || !equalValue(expected.name, actual.name, path + QStringLiteral(".name"), difference)
        || !equalValue(expected.size, actual.size, path + QStringLiteral(".size"), difference)
        || !equalValue(expected.background, actual.background, path + QStringLiteral(".background"), difference)
        || !equalValue(expected.layers.size(), actual.layers.size(),
                       path + QStringLiteral(".layers.size"), difference)) return false;
    for (size_t index = 0; index < expected.layers.size(); ++index) {
        const auto& left = expected.layers[index];
        const auto& right = actual.layers[index];
        if (!left || !right) {
            if (left != right && difference) {
                *difference = QStringLiteral("semantic mismatch at %1.layers[%2]")
                                  .arg(path).arg(qulonglong(index));
            }
            if (left != right) return false;
            continue;
        }
        if (!compareLayers(*left, *right, difference,
                           path + QStringLiteral(".layers[%1]").arg(qulonglong(index)))) return false;
    }
    return true;
}

} // namespace vt::test
