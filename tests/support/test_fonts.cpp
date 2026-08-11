#include "tests/support/test_fonts.h"

#include <QFontDatabase>
#include <QStringList>

namespace vt::test {

QString deterministicTestFamily(bool requireCyrillic)
{
    const auto writingSystem = requireCyrillic ? QFontDatabase::Cyrillic : QFontDatabase::Any;
    const QStringList families = QFontDatabase::families(writingSystem);
    for (const QString& preferred : {QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"),
                                    QStringLiteral("Segoe UI"), QStringLiteral("Arial")}) {
        if (families.contains(preferred, Qt::CaseInsensitive)) return preferred;
    }
    return families.value(0);
}

} // namespace vt::test
