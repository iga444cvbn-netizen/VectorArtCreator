#include "core/effects/text_range_rebaser.h"

namespace vt {

EffectScope TextRangeRebaser::rebase(const EffectScope& original,
                                     const QString& oldText,
                                     const QString& newText)
{
    if (original.kind != EffectScopeKind::TextRange) return original;
    EffectScope rebased = original;
    const int oldLength = oldText.size();
    const int newLength = newText.size();
    const int start = qBound(0, original.start, oldLength);
    const int end = qBound(start, original.end, oldLength);

    int prefix = 0;
    while (prefix < oldLength && prefix < newLength && oldText.at(prefix) == newText.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < oldLength - prefix && suffix < newLength - prefix
           && oldText.at(oldLength - 1 - suffix) == newText.at(newLength - 1 - suffix)) ++suffix;
    const int oldMiddleEnd = oldLength - suffix;
    const int newMiddleEnd = newLength - suffix;
    const int delta = newLength - oldLength;

    if (oldMiddleEnd <= start) { // replacement strictly before the target
        rebased.start = start + delta;
        rebased.end = end + delta;
    } else if (prefix >= end) { // replacement strictly after the target
        rebased.start = start;
        rebased.end = end;
    } else { // overlap: the complete replacement remains targeted
        rebased.start = qMin(start, prefix);
        rebased.end = qMax(newMiddleEnd, end + delta);
    }
    rebased.start = qBound(0, rebased.start, newLength);
    rebased.end = qBound(rebased.start, rebased.end, newLength);
    return rebased;
}

} // namespace vt
