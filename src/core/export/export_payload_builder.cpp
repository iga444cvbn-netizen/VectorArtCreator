#include "core/export/export_payload_builder.h"

#include <QTransform>

#include <utility>

namespace vt {
bool ExportPayloadBuilder::build(const Document&,
                                const Page& page,
                                const SceneGeometry& scene,
                                ExportScope scope,
                                const QStringList& selected,
                                VectorExportPayload* out,
                                QString* error)
{
    if (!out) {
        if (error) *error = QStringLiteral("No export payload destination.");
        return false;
    }
    if (scope == ExportScope::Selection && selected.isEmpty()) {
        if (error) *error = QStringLiteral("Select visible text with renderable geometry first.");
        return false;
    }

    VectorExportPayload result;
    result.scope = scope;
    result.pageSize = page.size;
    result.pageBackground = page.background;
    QStringList sourceTexts;
    bool hasBounds = false;

    for (const SceneObjectGeometry& object : scene.objects) {
        if (!object.visible || (scope == ExportScope::Selection && !selected.contains(object.objectId))) {
            continue;
        }
        for (const GeometryPiece& piece : object.geometry.pieces) {
            if (piece.path.isEmpty()) continue;
            const QRectF pieceBounds = piece.path.boundingRect();
            if (!pieceBounds.isValid() || pieceBounds.isEmpty()) continue;
            result.records.push_back({piece.path, object.fill,
                                      qBound<qreal>(0.0, object.fill.alphaF() * piece.opacityMultiplier, 1.0),
                                      object.objectId, object.sourceText});
            result.bounds = hasBounds ? result.bounds.united(pieceBounds) : pieceBounds;
            hasBounds = true;
        }
        // Text belongs to object identity. Equal values on two included
        // objects are not duplicates and must remain two payload entries.
        if (!object.sourceText.isEmpty()) {
            sourceTexts.push_back(object.sourceText);
        }
    }
    if (result.records.isEmpty() || !hasBounds || !result.bounds.isValid() || result.bounds.isEmpty()) {
        if (error) *error = scope == ExportScope::Selection
            ? QStringLiteral("The selection contains no visible renderable geometry.")
            : QStringLiteral("The current page has no renderable geometry.");
        return false;
    }

    result.plainText = sourceTexts.join(QLatin1Char('\n'));
    if (scope == ExportScope::Selection) {
        const QPointF origin = result.bounds.topLeft();
        QTransform translate;
        translate.translate(-origin.x(), -origin.y());
        for (VectorExportRecord& record : result.records) record.path = translate.map(record.path);
        result.bounds = QRectF(QPointF(), result.bounds.size());
    } else {
        if (page.size.isEmpty() || page.size.width() <= 0.0 || page.size.height() <= 0.0) {
            if (error) *error = QStringLiteral("The current page has invalid export dimensions.");
            return false;
        }
        result.bounds = QRectF(QPointF(), page.size);
    }
    *out = std::move(result);
    return true;
}
}
