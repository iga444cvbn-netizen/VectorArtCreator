#include "platform/windows/windows_vector_clipboard_service.h"

#include <windows.h>
#include <gdiplus.h>

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QWindow>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace vt {
namespace {

class Win32ClipboardOperations final : public WindowsClipboardOperations {
public:
    bool openClipboard(void* owner) override
    {
        return OpenClipboard(static_cast<HWND>(owner)) != FALSE;
    }

    void closeClipboard() override { static_cast<void>(CloseClipboard()); }
    bool emptyClipboard() override { return EmptyClipboard() != FALSE; }
    void retryDelay(unsigned milliseconds) override { Sleep(milliseconds); }

    void* allocateGlobal(std::size_t bytes) override
    {
        return GlobalAlloc(GMEM_MOVEABLE, static_cast<SIZE_T>(bytes));
    }

    void* lockGlobal(void* handle) override
    {
        return GlobalLock(static_cast<HGLOBAL>(handle));
    }

    void unlockGlobal(void* handle) override
    {
        static_cast<void>(GlobalUnlock(static_cast<HGLOBAL>(handle)));
    }

    void freeGlobal(void* handle) override
    {
        static_cast<void>(GlobalFree(static_cast<HGLOBAL>(handle)));
    }

    void* publish(unsigned format, void* handle) override
    {
        return SetClipboardData(static_cast<UINT>(format), static_cast<HANDLE>(handle));
    }

    unsigned registerFormat(const wchar_t* name) override
    {
        return static_cast<unsigned>(RegisterClipboardFormatW(name));
    }

    void deleteEnhancedMetafile(void* handle) override
    {
        static_cast<void>(DeleteEnhMetaFile(static_cast<HENHMETAFILE>(handle)));
    }
};

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
    ClipboardTransaction(void* owner,
                         WindowsClipboardOperations& operations,
                         const std::function<bool()>& openAttempt)
        : m_operations(operations)
    {
        for (int attempt = 0; attempt != 8 && !m_open; ++attempt) {
            m_open = openAttempt ? openAttempt() : m_operations.openClipboard(owner);
            if (!m_open) m_operations.retryDelay(12);
        }
    }
    ~ClipboardTransaction() { if (m_open) m_operations.closeClipboard(); }
    [[nodiscard]] bool open() const { return m_open; }
private:
    WindowsClipboardOperations& m_operations;
    bool m_open = false;
};

class GlobalMemory final {
public:
    GlobalMemory(WindowsClipboardOperations& operations, std::size_t size)
        : m_operations(operations), handle(m_operations.allocateGlobal(size))
    {
    }
    ~GlobalMemory() { if (handle) m_operations.freeGlobal(handle); }
    [[nodiscard]] void* release() { void* result = handle; handle = nullptr; return result; }
    WindowsClipboardOperations& m_operations;
    void* handle = nullptr;
};

bool setBytes(unsigned format,
              const QByteArray& bytes,
              WindowsClipboardOperations& operations)
{
    if (format == 0) return false;
    GlobalMemory memory(operations, static_cast<std::size_t>(bytes.size()));
    if (!memory.handle) return false;
    void* destination = operations.lockGlobal(memory.handle);
    if (!destination) return false;
    memcpy(destination, bytes.constData(), static_cast<size_t>(bytes.size()));
    operations.unlockGlobal(memory.handle);
    if (!operations.publish(format, memory.handle)) return false;
    static_cast<void>(memory.release()); // ownership transfers only on success
    return true;
}

bool addPath(Gdiplus::GraphicsPath& target,
             const QPainterPath& path,
             const WorkControl& work)
{
    const int count = path.elementCount();
    for (int index = 0; index < count; ++index) {
        if (!work.consume()) return false;
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
    return true;
}

HENHMETAFILE renderEmf(const VectorExportPayload& payload, const WorkControl& work)
{
    if (!work.consume()) return nullptr;
    GdiplusProcess gdiplus;
    if (!gdiplus.ok()) return nullptr;
    // A compatible memory DC avoids depending on a visible desktop surface;
    // Copy for Word must remain usable from headless/offscreen Qt sessions.
    HDC reference = CreateCompatibleDC(nullptr);
    if (!reference) return nullptr;
    const qreal unitsPerLogicalPixel = 2540.0 / VectorExportPayload::LogicalDpi;
    const Gdiplus::RectF frame(0.0f, 0.0f,
                               static_cast<Gdiplus::REAL>(payload.bounds.width() * unitsPerLogicalPixel),
                               static_cast<Gdiplus::REAL>(payload.bounds.height() * unitsPerLogicalPixel));
    HENHMETAFILE result = nullptr;
    {
        Gdiplus::Metafile metafile(reference, frame, Gdiplus::MetafileFrameUnitGdi,
                                   Gdiplus::EmfTypeEmfPlusDual, L"VectorTypographyEditor");
        if (metafile.GetLastStatus() == Gdiplus::Ok) {
            // GDI+ finalizes the recording when Graphics is destroyed.  Calling
            // GetHENHMETAFILE while Graphics is still alive returns null on the
            // headless Windows runner and can do the same in production.
            {
                Gdiplus::Graphics graphics(&metafile);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
                graphics.ScaleTransform(static_cast<Gdiplus::REAL>(unitsPerLogicalPixel),
                                        static_cast<Gdiplus::REAL>(unitsPerLogicalPixel));
                for (const VectorExportRecord& record : payload.records) {
                    if (!work.consume()) break;
                    Gdiplus::GraphicsPath path(Gdiplus::FillModeWinding);
                    if (!addPath(path, record.path, work)) break;
                    const QColor color = record.fill;
                    const BYTE alpha = static_cast<BYTE>(
                        qBound(0, qRound(record.opacity * 255.0), 255));
                    Gdiplus::SolidBrush brush(
                        Gdiplus::Color(alpha, color.red(), color.green(), color.blue()));
                    graphics.FillPath(&brush, &path);
                }
                graphics.Flush(Gdiplus::FlushIntentionSync);
            }
            result = metafile.GetHENHMETAFILE();
        }
    }
    DeleteDC(reference);
    return result;
}

QByteArray renderPng(const VectorExportPayload& payload, const WorkControl& work)
{
    constexpr qreal rasterDpi = 192.0;
    constexpr int maximumDimension = 8192;
    constexpr qint64 maximumPixels = 32LL * 1024LL * 1024LL;
    const qreal scale = rasterDpi / VectorExportPayload::LogicalDpi;
    int width = qCeil(payload.bounds.width() * scale);
    int height = qCeil(payload.bounds.height() * scale);
    if (width <= 0 || height <= 0 || width > maximumDimension || height > maximumDimension
        || static_cast<qint64>(width) * height > maximumPixels) return {};
    const qint64 pixels = static_cast<qint64>(width) * height;
    if (!work.consume((pixels + 1023) / 1024)) return {};
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(scale, scale);
    for (const VectorExportRecord& record : payload.records) {
        if (!work.consume(1 + record.path.elementCount())) {
            painter.end();
            return {};
        }
        QColor color = record.fill;
        color.setAlphaF(record.opacity);
        painter.fillPath(record.path, color);
    }
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG")) return {};
    if (!work.consume(qMax<qint64>(1, (bytes.size() + 1023) / 1024))) return {};
    return bytes;
}

QByteArray svgPathData(const QPainterPath& path, const WorkControl& work)
{
    QByteArray data;
    for (int index = 0; index < path.elementCount(); ++index) {
        if (!work.consume()) return {};
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

QByteArray svgFallback(const VectorExportPayload& payload, const WorkControl& work)
{
    if (!work.consume()) return {};
    QByteArray bytes("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 ");
    bytes += QByteArray::number(payload.bounds.width()); bytes += ' ';
    bytes += QByteArray::number(payload.bounds.height()); bytes += "\">";
    for (const VectorExportRecord& record : payload.records) {
        if (!work.consume()) return {};
        const QByteArray path = svgPathData(record.path, work);
        if (!work.isRunning()) return {};
        bytes += "<path fill=\"" + record.fill.name(QColor::HexRgb).toUtf8()
                 + "\" fill-opacity=\"" + QByteArray::number(record.opacity)
                 + "\" fill-rule=\"nonzero\" d=\"" + path + "\"/>";
    }
    return bytes + "</svg>";
}

ClipboardPublicationResult interruptedResult(const WorkControl& work)
{
    if (work.status() == WorkControlStatus::Cancelled) {
        return {ClipboardPublicationStatus::Cancelled, work.interruptionMessage()};
    }
    return {ClipboardPublicationStatus::Failure, work.interruptionMessage()};
}

} // namespace

namespace {

ClipboardPublicationResult copyForOfficeImpl(const VectorExportPayload& payload,
                                             WindowsClipboardOperations& operations,
                                             const WorkControl& work,
                                             const std::function<bool()>& openAttempt)
{
    if (payload.records.isEmpty() || payload.bounds.isEmpty()) {
        return ClipboardPublicationResult::fromFormats(
            false, false, false, false, QStringLiteral("There is no valid vector geometry to copy."));
    }
    if (!work.consume()) return interruptedResult(work);
    HENHMETAFILE metafile = renderEmf(payload, work);
    if (!work.isRunning()) {
        if (metafile) operations.deleteEnhancedMetafile(metafile);
        return interruptedResult(work);
    }
    if (!metafile) {
        return ClipboardPublicationResult::fromFormats(
            false, false, false, false,
            QStringLiteral("Could not create the Windows EMF+ clipboard artwork."));
    }
    const QByteArray png = renderPng(payload, work);
    if (!work.isRunning()) {
        operations.deleteEnhancedMetafile(metafile);
        return interruptedResult(work);
    }
    if (png.isEmpty()) {
        operations.deleteEnhancedMetafile(metafile);
        return ClipboardPublicationResult::fromFormats(
            false, false, false, false,
            QStringLiteral("The raster fallback would exceed the production size limit."));
    }
    const QByteArray svg = svgFallback(payload, work);
    if (!work.isRunning()) {
        operations.deleteEnhancedMetafile(metafile);
        return interruptedResult(work);
    }
    QWindow* activeWindow = QGuiApplication::focusWindow();
    if (!activeWindow) {
        const auto windows = QGuiApplication::topLevelWindows();
        activeWindow = windows.isEmpty() ? nullptr : windows.front();
    }
    void* owner = activeWindow ? reinterpret_cast<void*>(activeWindow->winId()) : nullptr;

    // Final cooperative checkpoint. After this succeeds publication is a short,
    // non-interruptible ownership transaction: no cancelled result can leave a
    // half-published clipboard behind.
    if (!work.consume()) {
        operations.deleteEnhancedMetafile(metafile);
        return interruptedResult(work);
    }
    ClipboardTransaction transaction(owner, operations, openAttempt);
    if (!transaction.open() || !operations.emptyClipboard()) {
        operations.deleteEnhancedMetafile(metafile);
        return ClipboardPublicationResult::fromFormats(
            false, false, false, false,
            QStringLiteral("The clipboard is busy. Close the app using it and try again."));
    }
    if (!operations.publish(CF_ENHMETAFILE, metafile)) {
        operations.deleteEnhancedMetafile(metafile);
        return ClipboardPublicationResult::fromFormats(
            false, false, false, false,
            QStringLiteral("Windows rejected the enhanced metafile clipboard format."));
    }
    metafile = nullptr; // Clipboard owns the handle only after a successful transfer.
    const unsigned svgFormat = operations.registerFormat(L"image/svg+xml");
    const unsigned pngFormat = operations.registerFormat(L"PNG");
    const bool svgPublished = setBytes(svgFormat, svg, operations);
    const bool pngPublished = setBytes(pngFormat, png, operations);
    bool textPublished = false;
    const std::wstring wideText = payload.plainText.toStdWString();
    GlobalMemory unicode(operations, (wideText.size() + 1) * sizeof(wchar_t));
    if (unicode.handle) {
        void* data = operations.lockGlobal(unicode.handle);
        if (data) {
            memcpy(data, wideText.c_str(), (wideText.size() + 1) * sizeof(wchar_t));
            operations.unlockGlobal(unicode.handle);
            if (operations.publish(CF_UNICODETEXT, unicode.handle)) {
                static_cast<void>(unicode.release()); // success transfers ownership
                textPublished = true;
            }
        }
    }
    return ClipboardPublicationResult::fromFormats(
        true, svgPublished, pngPublished, textPublished);
}

} // namespace

ClipboardPublicationResult WindowsVectorClipboardService::copyForOffice(
    const VectorExportPayload& payload,
    const WorkControl& work)
{
    Win32ClipboardOperations operations;
    return copyForOfficeImpl(payload, operations, work, {});
}

ClipboardPublicationResult WindowsVectorClipboardService::copyForOfficeWithOpenAttemptForTesting(
    const VectorExportPayload& payload,
    const std::function<bool()>& openAttempt)
{
    Win32ClipboardOperations operations;
    return copyForOfficeImpl(payload, operations, WorkControl::withBudget(), openAttempt);
}

ClipboardPublicationResult WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
    const VectorExportPayload& payload,
    WindowsClipboardOperations& operations,
    const WorkControl& work)
{
    return copyForOfficeImpl(payload, operations, work, {});
}

} // namespace vt
