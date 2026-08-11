#include "platform/windows/windows_vector_clipboard_service.h"

#include <windows.h>
#include <gdiplus.h>

#include <QBuffer>
#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace vt {
namespace {

class GdiplusProcess final {
public:
    GdiplusProcess()
    {
        Gdiplus::GdiplusStartupInput input;
        m_status = Gdiplus::GdiplusStartup(&m_token, &input, nullptr);
    }
    ~GdiplusProcess() { if (m_status == Gdiplus::Ok) Gdiplus::GdiplusShutdown(m_token); }
    [[nodiscard]] bool ok() const { return m_status == Gdiplus::Ok; }
private:
    ULONG_PTR m_token = 0;
    Gdiplus::Status m_status = Gdiplus::GenericError;
};

class ClipboardTransaction final {
public:
    ClipboardTransaction()
    {
        for (int attempt = 0; attempt != 8 && !m_open; ++attempt) {
            m_open = OpenClipboard(nullptr) != FALSE;
            if (!m_open) Sleep(12);
        }
    }
    ~ClipboardTransaction() { if (m_open) CloseClipboard(); }
    [[nodiscard]] bool open() const { return m_open; }
private:
    bool m_open = false;
};

class GlobalMemory final {
public:
    explicit GlobalMemory(SIZE_T size) : handle(GlobalAlloc(GMEM_MOVEABLE, size)) {}
    ~GlobalMemory() { if (handle) GlobalFree(handle); }
    [[nodiscard]] HGLOBAL release() { HGLOBAL result = handle; handle = nullptr; return result; }
    HGLOBAL handle = nullptr;
};

bool setBytes(UINT format, const QByteArray& bytes)
{
    GlobalMemory memory(static_cast<SIZE_T>(bytes.size()));
    if (!memory.handle) return false;
    void* destination = GlobalLock(memory.handle);
    if (!destination) return false;
    memcpy(destination, bytes.constData(), static_cast<size_t>(bytes.size()));
    GlobalUnlock(memory.handle);
    return SetClipboardData(format, memory.release()) != nullptr;
}

void addPath(Gdiplus::GraphicsPath& target, const QPainterPath& path)
{
    const int count = path.elementCount();
    for (int index = 0; index < count; ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        if (element.type == QPainterPath::MoveToElement) {
            target.StartFigure();
            target.AddLine(static_cast<Gdiplus::REAL>(element.x), static_cast<Gdiplus::REAL>(element.y),
                           static_cast<Gdiplus::REAL>(element.x), static_cast<Gdiplus::REAL>(element.y));
        } else if (element.type == QPainterPath::LineToElement) {
            const QPainterPath::Element previous = path.elementAt(index - 1);
            target.AddLine(static_cast<Gdiplus::REAL>(previous.x), static_cast<Gdiplus::REAL>(previous.y),
                           static_cast<Gdiplus::REAL>(element.x), static_cast<Gdiplus::REAL>(element.y));
        } else if (element.type == QPainterPath::CurveToElement && index + 2 < count) {
            const QPainterPath::Element previous = path.elementAt(index - 1);
            const QPainterPath::Element control2 = path.elementAt(index + 1);
            const QPainterPath::Element end = path.elementAt(index + 2);
            target.AddBezier(static_cast<Gdiplus::REAL>(previous.x), static_cast<Gdiplus::REAL>(previous.y),
                             static_cast<Gdiplus::REAL>(element.x), static_cast<Gdiplus::REAL>(element.y),
                             static_cast<Gdiplus::REAL>(control2.x), static_cast<Gdiplus::REAL>(control2.y),
                             static_cast<Gdiplus::REAL>(end.x), static_cast<Gdiplus::REAL>(end.y));
            index += 2;
        }
    }
}

HENHMETAFILE renderEmf(const VectorExportPayload& payload)
{
    GdiplusProcess gdiplus;
    if (!gdiplus.ok()) return nullptr;
    HDC reference = GetDC(nullptr);
    if (!reference) return nullptr;
    const qreal unitsPerLogicalPixel = 2540.0 / VectorExportPayload::LogicalDpi;
    const Gdiplus::RectF frame(0.0f, 0.0f,
                               static_cast<Gdiplus::REAL>(payload.bounds.width() * unitsPerLogicalPixel),
                               static_cast<Gdiplus::REAL>(payload.bounds.height() * unitsPerLogicalPixel));
    HENHMETAFILE result = nullptr;
    {
        Gdiplus::Metafile metafile(reference, &frame, Gdiplus::MetafileFrameUnitGdi,
                                   Gdiplus::EmfTypeEmfPlusDual, L"VectorTypographyEditor");
        ReleaseDC(nullptr, reference);
        Gdiplus::Graphics graphics(&metafile);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        graphics.ScaleTransform(static_cast<Gdiplus::REAL>(unitsPerLogicalPixel),
                                static_cast<Gdiplus::REAL>(unitsPerLogicalPixel));
        for (const VectorExportRecord& record : payload.records) {
            Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
            addPath(path, record.path);
            const QColor color = record.fill;
            const BYTE alpha = static_cast<BYTE>(qBound(0, qRound(record.opacity * 255.0), 255));
            Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, color.red(), color.green(), color.blue()));
            graphics.FillPath(&brush, &path);
        }
        graphics.Flush(Gdiplus::FlushIntentionSync);
        result = metafile.GetHENHMETAFILE();
    }
    return result;
}

QByteArray renderPng(const VectorExportPayload& payload)
{
    constexpr qreal rasterDpi = 192.0;
    constexpr int maximumDimension = 8192;
    constexpr qint64 maximumPixels = 32LL * 1024LL * 1024LL;
    const qreal scale = rasterDpi / VectorExportPayload::LogicalDpi;
    int width = qCeil(payload.bounds.width() * scale);
    int height = qCeil(payload.bounds.height() * scale);
    if (width <= 0 || height <= 0 || width > maximumDimension || height > maximumDimension
        || static_cast<qint64>(width) * height > maximumPixels) return {};
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    for (const VectorExportRecord& record : payload.records) {
        QColor color = record.fill;
        color.setAlphaF(record.opacity);
        painter.fillPath(record.path, color);
    }
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray svgPathData(const QPainterPath& path)
{
    QByteArray data;
    for (int index = 0; index < path.elementCount(); ++index) {
        const QPainterPath::Element element = path.elementAt(index);
        const auto number = [](qreal value) { return QByteArray::number(value, 'g', 12); };
        if (element.type == QPainterPath::MoveToElement) {
            data += "M" + number(element.x) + ' ' + number(element.y) + ' ';
        } else if (element.type == QPainterPath::LineToElement) {
            data += "L" + number(element.x) + ' ' + number(element.y) + ' ';
        } else if (element.type == QPainterPath::CurveToElement && index + 2 < path.elementCount()) {
            const QPainterPath::Element control2 = path.elementAt(index + 1);
            const QPainterPath::Element end = path.elementAt(index + 2);
            data += "C" + number(element.x) + ' ' + number(element.y) + ' '
                    + number(control2.x) + ' ' + number(control2.y) + ' '
                    + number(end.x) + ' ' + number(end.y) + ' ';
            index += 2;
        }
    }
    return data;
}

QByteArray svgFallback(const VectorExportPayload& payload)
{
    QByteArray bytes("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ");
    bytes += QByteArray::number(payload.bounds.width()); bytes += ' ';
    bytes += QByteArray::number(payload.bounds.height()); bytes += "\">";
    for (const VectorExportRecord& record : payload.records) {
        bytes += "<path fill=\"" + record.fill.name(QColor::HexRgb).toUtf8()
                 + "\" fill-opacity=\"" + QByteArray::number(record.opacity)
                 + "\" fill-rule=\"nonzero\" d=\"" + svgPathData(record.path) + "\"/>";
    }
    return bytes + "</svg>";
}

} // namespace

bool WindowsVectorClipboardService::copyForOffice(const VectorExportPayload& payload, QString* error)
{
    if (payload.records.isEmpty() || payload.bounds.isEmpty()) {
        if (error) *error = QStringLiteral("There is no valid vector geometry to copy.");
        return false;
    }
    HENHMETAFILE metafile = renderEmf(payload);
    if (!metafile) {
        if (error) *error = QStringLiteral("Could not create the Windows EMF+ clipboard artwork.");
        return false;
    }
    const QByteArray png = renderPng(payload);
    if (png.isEmpty()) {
        DeleteEnhMetaFile(metafile);
        if (error) *error = QStringLiteral("The raster fallback would exceed the production size limit.");
        return false;
    }
    const QByteArray svg = svgFallback(payload);
    ClipboardTransaction transaction;
    if (!transaction.open() || !EmptyClipboard()) {
        DeleteEnhMetaFile(metafile);
        if (error) *error = QStringLiteral("The clipboard is busy. Close the app using it and try again.");
        return false;
    }
    if (!SetClipboardData(CF_ENHMETAFILE, metafile)) {
        DeleteEnhMetaFile(metafile);
        if (error) *error = QStringLiteral("Windows rejected the enhanced metafile clipboard format.");
        return false;
    }
    metafile = nullptr; // Clipboard owns the handle after a successful transfer.
    const UINT svgFormat = RegisterClipboardFormatW(L"image/svg+xml");
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    bool fallbackOk = setBytes(svgFormat, svg) && setBytes(pngFormat, png);
    const std::wstring wideText = payload.plainText.toStdWString();
    GlobalMemory unicode((wideText.size() + 1) * sizeof(wchar_t));
    if (unicode.handle) {
        void* data = GlobalLock(unicode.handle);
        if (data) {
            memcpy(data, wideText.c_str(), (wideText.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(unicode.handle);
            fallbackOk = SetClipboardData(CF_UNICODETEXT, unicode.release()) != nullptr && fallbackOk;
        }
    }
    if (!fallbackOk && error) *error = QStringLiteral("Vector copied, but one or more SVG/PNG/text fallbacks could not be published.");
    return true;
}

} // namespace vt
