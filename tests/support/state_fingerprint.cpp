#include "tests/support/state_fingerprint.h"

#include "core/serialization/project_serializer.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace vt::test {
namespace {

void removeTransient(QJsonObject* document)
{
    document->remove(QStringLiteral("createdAt"));
    document->remove(QStringLiteral("modifiedAt"));
    document->remove(QStringLiteral("activeObjectId"));
}

} // namespace

QString semanticFingerprint(const Document& document)
{
    QJsonObject json = ProjectSerializer::toJson(document).object();
    removeTransient(&json);
    // Serializer writes page/layer/effect ordering in semantic order, so a
    // compact JSON document is a stable comparison without byte-comparing a
    // persisted project file or transient metadata.
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

bool semanticallyEqual(const Document& left, const Document& right, QString* difference)
{
    const QString leftValue = semanticFingerprint(left);
    const QString rightValue = semanticFingerprint(right);
    if (leftValue == rightValue) return true;
    if (difference) {
        *difference = QStringLiteral("semantic fingerprints differ\nleft: %1\nright: %2")
            .arg(leftValue, rightValue);
    }
    return false;
}

} // namespace vt::test
