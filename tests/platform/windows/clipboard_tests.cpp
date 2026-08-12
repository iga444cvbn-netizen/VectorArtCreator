#include "platform/vector_clipboard_service.h"
#include "platform/windows/windows_vector_clipboard_service.h"

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <QVector>

#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace vt;

#ifdef Q_OS_WIN
namespace {

class FakeClipboardOperations final : public WindowsClipboardOperations {
public:
    struct Allocation {
        explicit Allocation(std::size_t size)
            : bytes(static_cast<qsizetype>(size), '\0')
        {
        }
        QByteArray bytes;
        bool freed = false;
        bool transferred = false;
    };

    ~FakeClipboardOperations() override
    {
        if (transferredMetafile) {
            DeleteEnhMetaFile(static_cast<HENHMETAFILE>(transferredMetafile));
        }
    }

    bool openClipboard(void*) override
    {
        ++openCalls;
        return !failOpen;
    }
    void closeClipboard() override { ++closeCalls; }
    bool emptyClipboard() override
    {
        ++emptyCalls;
        return !failEmpty;
    }
    void retryDelay(unsigned) override { ++retryCalls; }

    void* allocateGlobal(std::size_t bytes) override
    {
        ++allocateCalls;
        if (allocateCalls == failAllocateCall) return nullptr;
        allocations.push_back(std::make_unique<Allocation>(bytes));
        return allocations.back().get();
    }
    void* lockGlobal(void* handle) override
    {
        ++lockCalls;
        if (lockCalls == failLockCall) return nullptr;
        return static_cast<Allocation*>(handle)->bytes.data();
    }
    void unlockGlobal(void*) override { ++unlockCalls; }
    void freeGlobal(void* handle) override
    {
        ++freeCalls;
        static_cast<Allocation*>(handle)->freed = true;
    }
    void* publish(unsigned format, void* handle) override
    {
        ++publishCalls;
        if (publishCalls == failPublishCall) return nullptr;
        if (format == CF_ENHMETAFILE) {
            transferredMetafile = handle;
        } else {
            static_cast<Allocation*>(handle)->transferred = true;
        }
        return handle;
    }
    unsigned registerFormat(const wchar_t*) override
    {
        ++registerCalls;
        if (registerCalls == failRegisterCall) return 0;
        return static_cast<unsigned>(0xC000 + registerCalls);
    }
    void deleteEnhancedMetafile(void* handle) override
    {
        ++deleteMetafileCalls;
        DeleteEnhMetaFile(static_cast<HENHMETAFILE>(handle));
    }

    [[nodiscard]] int transferredAllocationCount() const
    {
        int count = 0;
        for (const auto& allocation : allocations) count += allocation->transferred ? 1 : 0;
        return count;
    }

    bool failOpen = false;
    bool failEmpty = false;
    int failAllocateCall = -1;
    int failLockCall = -1;
    int failPublishCall = -1;
    int failRegisterCall = -1;
    int openCalls = 0;
    int closeCalls = 0;
    int emptyCalls = 0;
    int retryCalls = 0;
    int allocateCalls = 0;
    int lockCalls = 0;
    int unlockCalls = 0;
    int freeCalls = 0;
    int publishCalls = 0;
    int registerCalls = 0;
    int deleteMetafileCalls = 0;
    void* transferredMetafile = nullptr;
    std::vector<std::unique_ptr<Allocation>> allocations;
};

VectorExportPayload smallPayload()
{
    VectorExportPayload payload;
    payload.bounds = QRectF(0, 0, 32, 16);
    payload.plainText = QStringLiteral("clipboard");
    VectorExportRecord record;
    record.path.addRect(payload.bounds);
    record.fill = Qt::black;
    record.sourceText = payload.plainText;
    payload.records = {record};
    return payload;
}

} // namespace
#endif

class WindowsClipboardTests final : public QObject {
    Q_OBJECT

private slots:
    void publicationResultClassification();
    void copyForWordPublishesPortableFormats();
    void busyClipboardIsAProductionFailure();
    void oversizedRasterFallbackIsAProductionFailure();
    void injectedOperationsClassifyFailuresAndOwnership();
    void cancellationStopsBeforeClipboardPublication();
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

    const ClipboardPublicationResult cancelled{
        ClipboardPublicationStatus::Cancelled, QStringLiteral("cancelled")};
    QVERIFY(cancelled.cancelled());
    QVERIFY(!cancelled.succeeded());
}

void WindowsClipboardTests::injectedOperationsClassifyFailuresAndOwnership()
{
#ifndef Q_OS_WIN
    QSKIP("Windows clipboard formats are only meaningful on Windows.");
#else
    const VectorExportPayload payload = smallPayload();

    {
        FakeClipboardOperations operations;
        const auto result = WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
            payload, operations);
        QVERIFY2(result.complete(), qPrintable(result.message));
        QCOMPARE(operations.publishCalls, 4);
        QCOMPARE(operations.transferredAllocationCount(), 3);
        QCOMPARE(operations.freeCalls, 0);
        QCOMPARE(operations.deleteMetafileCalls, 0);
        QVERIFY(operations.transferredMetafile);
    }
    {
        FakeClipboardOperations operations;
        operations.failPublishCall = 1;
        const auto result = WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
            payload, operations);
        QCOMPARE(result.status, ClipboardPublicationStatus::Failure);
        QCOMPARE(operations.deleteMetafileCalls, 1);
        QCOMPARE(operations.allocateCalls, 0);
    }
    for (int failureKind = 0; failureKind < 4; ++failureKind) {
        FakeClipboardOperations operations;
        if (failureKind == 0) operations.failRegisterCall = 1;
        if (failureKind == 1) operations.failAllocateCall = 1;
        if (failureKind == 2) operations.failLockCall = 1;
        if (failureKind == 3) operations.failPublishCall = 2;
        const auto result = WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
            payload, operations);
        QCOMPARE(result.status, ClipboardPublicationStatus::Partial);
        QVERIFY(result.message.contains(QStringLiteral("SVG")));
        QCOMPARE(operations.deleteMetafileCalls, 0);
        QVERIFY(operations.transferredMetafile);
        if (failureKind == 1) QCOMPARE(operations.freeCalls, 0);
        if (failureKind == 2 || failureKind == 3) QCOMPARE(operations.freeCalls, 1);
    }
    {
        FakeClipboardOperations operations;
        operations.failEmpty = true;
        const auto result = WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
            payload, operations);
        QCOMPARE(result.status, ClipboardPublicationStatus::Failure);
        QCOMPARE(operations.deleteMetafileCalls, 1);
        QCOMPARE(operations.publishCalls, 0);
        QCOMPARE(operations.closeCalls, 1);
    }
#endif
}

void WindowsClipboardTests::cancellationStopsBeforeClipboardPublication()
{
#ifndef Q_OS_WIN
    QSKIP("Windows clipboard formats are only meaningful on Windows.");
#else
    FakeClipboardOperations operations;
    const WorkControl work = WorkControl::withBudget(1000);
    int checkpoints = 0;
    work.setCheckpointCallback([work, &checkpoints](qint64) {
        // The third checkpoint occurs after the EMF recording exists and at
        // its first record. Cancellation must delete it before publication.
        if (++checkpoints == 3) work.cancel();
    });
    const auto result = WindowsVectorClipboardService::copyForOfficeWithOperationsForTesting(
        smallPayload(), operations, work);
    QCOMPARE(result.status, ClipboardPublicationStatus::Cancelled);
    QCOMPARE(operations.openCalls, 0);
    QCOMPARE(operations.emptyCalls, 0);
    QCOMPARE(operations.publishCalls, 0);
    QCOMPARE(operations.deleteMetafileCalls, 1);
#endif
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
