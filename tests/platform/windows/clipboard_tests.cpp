#include "platform/vector_clipboard_service.h"
#include "platform/windows/windows_vector_clipboard_service.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vt;

class WindowsClipboardTests final : public QObject {
    Q_OBJECT

private slots:
    void publicationResultClassification();
    void copyForWordPublishesPortableFormats();
    void busyClipboardIsAProductionFailure();
    void oversizedRasterFallbackIsAProductionFailure();
};

void WindowsClipboardTests::publicationResultClassification()
{
    const auto complete = ClipboardPublicationResult::fromFormats(true, true, true, true);
    QCOMPARE(complete.status, ClipboardPublicationStatus::Complete);
    QVERIFY(complete.succeeded());
    QVERIFY(complete.complete());

    const auto partial = ClipboardPublicationResult::fromFormats(true, false, true, false);
    QCOMPARE(partial.status, ClipboardPublicationStatus::Partial);
    QVERIFY(partial.succeeded());
    QVERIFY(!partial.complete());
    QVERIFY(partial.message.contains(QStringLiteral("SVG")));
    QVERIFY(partial.message.contains(QStringLiteral("Unicode text")));

    const auto failure = ClipboardPublicationResult::fromFormats(
        false, false, false, false, QStringLiteral("injected failure"));
    QCOMPARE(failure.status, ClipboardPublicationStatus::Failure);
    QVERIFY(!failure.succeeded());
    QCOMPARE(failure.message, QStringLiteral("injected failure"));
}

void WindowsClipboardTests::copyForWordPublishesPortableFormats()
{
#ifndef Q_OS_WIN
    QSKIP("Windows clipboard formats are only meaningful on Windows.");
#else
    VectorExportPayload payload;
    payload.scope = ExportScope::Selection;
    payload.bounds = QRectF(0, 0, 64, 32);
    payload.plainText = QString::fromUtf8("\320\237\321\200\320\270\320\262\320\265\321\202");
    VectorExportRecord record;
    record.path.addRect(payload.bounds);
    record.fill = QColor(20, 80, 180, 180);
    record.opacity = 0.7;
    record.sourceText = payload.plainText;
    payload.records.push_back(record);
    const ClipboardPublicationResult result = VectorClipboardService::copyForOfficeResult(payload);
    QVERIFY2(result.complete(), qPrintable(result.message));
    const ClipboardPublicationResult repeated = VectorClipboardService::copyForOfficeResult(payload);
    QVERIFY2(repeated.complete(), qPrintable(repeated.message));
    QVERIFY(OpenClipboard(nullptr));
    const UINT svgFormat = RegisterClipboardFormatW(L"image/svg+xml");
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    HENHMETAFILE emf = static_cast<HENHMETAFILE>(GetClipboardData(CF_ENHMETAFILE));
    QVERIFY(emf);
    ENHMETAHEADER header{};
    QVERIFY(GetEnhMetaFileHeader(emf, sizeof(header), &header) > 0);
    QVERIFY(header.rclBounds.right > header.rclBounds.left);
    const HGLOBAL textHandle = static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT));
    QVERIFY(textHandle);
    const auto* text = static_cast<const wchar_t*>(GlobalLock(textHandle));
    QVERIFY(text);
    QCOMPARE(QString::fromWCharArray(text), payload.plainText);
    GlobalUnlock(textHandle);
    const HGLOBAL pngHandle = static_cast<HGLOBAL>(GetClipboardData(pngFormat));
    QVERIFY(pngHandle);
    const void* pngBytes = GlobalLock(pngHandle);
    QVERIFY(pngBytes);
    const SIZE_T pngSize = GlobalSize(pngHandle);
    const QImage image = QImage::fromData(static_cast<const uchar*>(pngBytes), static_cast<int>(pngSize), "PNG");
    GlobalUnlock(pngHandle);
    QVERIFY(!image.isNull());
    QVERIFY(image.width() > 0 && image.height() > 0);
    const HGLOBAL svgHandle = static_cast<HGLOBAL>(GetClipboardData(svgFormat));
    QVERIFY(svgHandle);
    const char* svgBytes = static_cast<const char*>(GlobalLock(svgHandle));
    QVERIFY(svgBytes);
    const QByteArray svg(svgBytes, static_cast<int>(GlobalSize(svgHandle)));
    GlobalUnlock(svgHandle);
    QVERIFY(svg.contains("<svg"));
    QVERIFY(svg.contains("<path"));
    QVERIFY(CloseClipboard());
#endif
}

void WindowsClipboardTests::busyClipboardIsAProductionFailure()
{
#ifndef Q_OS_WIN
    QSKIP("Windows clipboard formats are only meaningful on Windows.");
#else
    VectorExportPayload payload;
    payload.bounds = QRectF(0, 0, 32, 16);
    VectorExportRecord record;
    record.path.addRect(payload.bounds);
    record.fill = Qt::black;
    payload.records = {record};
    int openAttempts = 0;
    const ClipboardPublicationResult result =
        WindowsVectorClipboardService::copyForOfficeWithOpenAttemptForTesting(
            payload, [&openAttempts] {
                ++openAttempts;
                return false;
            });
    QCOMPARE(openAttempts, 8);
    QCOMPARE(result.status, ClipboardPublicationStatus::Failure);
    QVERIFY(result.message.contains(QStringLiteral("busy"), Qt::CaseInsensitive));
#endif
}

void WindowsClipboardTests::oversizedRasterFallbackIsAProductionFailure()
{
#ifndef Q_OS_WIN
    QSKIP("Windows clipboard formats are only meaningful on Windows.");
#else
    VectorExportPayload payload;
    payload.bounds = QRectF(0, 0, 10000, 10000);
    VectorExportRecord record;
    record.path.addRect(payload.bounds);
    record.fill = Qt::black;
    payload.records = {record};
    const ClipboardPublicationResult result = VectorClipboardService::copyForOfficeResult(payload);
    QCOMPARE(result.status, ClipboardPublicationStatus::Failure);
    QVERIFY(result.message.contains(QStringLiteral("size limit")));
#endif
}

QTEST_MAIN(WindowsClipboardTests)
#include "clipboard_tests.moc"
