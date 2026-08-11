#pragma once
#include "core/export/export_scope.h"
#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <QSizeF>
#include <QVector>
namespace vt {
struct VectorExportRecord { QPainterPath path; QColor fill; qreal opacity=1.0; QString sourceObjectId; QString sourceText; };
struct VectorExportPayload { ExportScope scope=ExportScope::Selection; QVector<VectorExportRecord> records; QRectF bounds; QSizeF pageSize; QColor pageBackground; QString plainText; static constexpr qreal LogicalDpi=96.0; };
}
