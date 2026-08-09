#include "core/export/svg_exporter.h"

#include <QSaveFile>
#include <QXmlStreamWriter>

namespace vt {

QString SvgExporter::formatId() const
{
    return QStringLiteral("svg");
}

QString SvgExporter::number(qreal value)
{
    return QString::number(value, 'f', 4);
}

QString SvgExporter::pathData(const QPainterPath& path)
{
    QString data;
    data.reserve(path.elementCount() * 24);

    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            data += QStringLiteral("M %1 %2 ").arg(number(element.x), number(element.y));
            break;
        case QPainterPath::LineToElement:
            data += QStringLiteral("L %1 %2 ").arg(number(element.x), number(element.y));
            break;
        case QPainterPath::CurveToElement:
            if (index + 2 < path.elementCount()) {
                const QPainterPath::Element control1 = path.elementAt(index + 1);
                const QPainterPath::Element end = path.elementAt(index + 2);
                data += QStringLiteral("C %1 %2 %3 %4 %5 %6 ")
                            .arg(number(element.x), number(element.y))
                            .arg(number(control1.x), number(control1.y))
                            .arg(number(end.x), number(end.y));
                index += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            // Consumed together with the preceding CurveToElement.
            break;
        }
    }
    return data.trimmed();
}

bool SvgExporter::exportGeometry(const Document& document,
                                 const VectorGeometry& geometry,
                                 const QString& filePath,
                                 QString* error) const
{
    const TextObject& textObject = document.primaryTextObject();
    QRectF viewBounds = geometry.bounds;
    if (viewBounds.isEmpty()) {
        viewBounds = geometry.referenceBounds;
    }
    if (viewBounds.isEmpty()) {
        viewBounds = QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal padding = qMax<qreal>(1.0, geometry.referenceHeight * 0.05);
    viewBounds.adjust(-padding, -padding, padding, padding);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open SVG for writing: %1").arg(file.errorString());
        }
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeAttribute(QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    xml.writeAttribute(QStringLiteral("viewBox"), QStringLiteral("%1 %2 %3 %4")
                           .arg(number(viewBounds.left()), number(viewBounds.top()),
                                number(viewBounds.width()), number(viewBounds.height())));
    xml.writeAttribute(QStringLiteral("width"), number(viewBounds.width()));
    xml.writeAttribute(QStringLiteral("height"), number(viewBounds.height()));

    xml.writeStartElement(QStringLiteral("path"));
    xml.writeAttribute(QStringLiteral("d"), pathData(geometry.combinedPath()));
    xml.writeAttribute(QStringLiteral("fill"), textObject.fill.name(QColor::HexRgb));
    xml.writeAttribute(QStringLiteral("fill-opacity"), number(textObject.fill.alphaF()));
    xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("nonzero"));
    xml.writeEndElement();

    xml.writeEndElement();
    xml.writeEndDocument();

    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not commit SVG file: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

bool SvgExporter::exportScene(const Document& document,
                              const SceneGeometry& scene,
                              const QString& filePath,
                              QString* error) const
{
    Q_UNUSED(document);
    QRectF viewBounds = scene.pageSize.isEmpty() ? scene.bounds : QRectF(QPointF(0.0, 0.0), scene.pageSize);
    if (viewBounds.isEmpty()) {
        viewBounds = scene.bounds;
    }
    if (viewBounds.isEmpty()) {
        viewBounds = QRectF(0.0, 0.0, 1.0, 1.0);
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("Cannot open SVG for writing: %1").arg(file.errorString());
        }
        return false;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("svg"));
    xml.writeAttribute(QStringLiteral("xmlns"), QStringLiteral("http://www.w3.org/2000/svg"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    xml.writeAttribute(QStringLiteral("viewBox"), QStringLiteral("%1 %2 %3 %4")
                           .arg(number(viewBounds.left()), number(viewBounds.top()),
                                number(viewBounds.width()), number(viewBounds.height())));
    xml.writeAttribute(QStringLiteral("width"), number(viewBounds.width()));
    xml.writeAttribute(QStringLiteral("height"), number(viewBounds.height()));

    for (const SceneObjectGeometry& object : scene.objects) {
        if (!object.visible || !object.geometry.hasVisibleGeometry()) {
            continue;
        }
        xml.writeStartElement(QStringLiteral("path"));
        xml.writeAttribute(QStringLiteral("id"), object.objectId);
        xml.writeAttribute(QStringLiteral("d"), pathData(object.geometry.combinedPath()));
        xml.writeAttribute(QStringLiteral("fill"), object.fill.name(QColor::HexRgb));
        xml.writeAttribute(QStringLiteral("fill-opacity"), number(object.fill.alphaF()));
        xml.writeAttribute(QStringLiteral("fill-rule"), QStringLiteral("nonzero"));
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();
    if (!file.commit()) {
        if (error) {
            *error = QStringLiteral("Could not commit SVG file: %1").arg(file.errorString());
        }
        return false;
    }
    return true;
}

} // namespace vt
