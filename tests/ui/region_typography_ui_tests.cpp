#include "core/document/document.h"
#include "ui/slider_spin_box.h"
#include "ui/typography_panel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace vt;

class RegionTypographyUiTests final : public QObject {
    Q_OBJECT

private slots:
    void regionControlsExposePersistentSettings();
};

void RegionTypographyUiTests::regionControlsExposePersistentSettings()
{
    TypographyPanel panel;
    auto* mode = panel.findChild<QComboBox*>(QStringLiteral("typographyLayoutMode"));
    auto* horizontal = panel.findChild<QComboBox*>(QStringLiteral("regionHorizontalAlignment"));
    auto* vertical = panel.findChild<QComboBox*>(QStringLiteral("regionVerticalAlignment"));
    auto* overflow = panel.findChild<QComboBox*>(QStringLiteral("regionOverflow"));
    auto* padding = panel.findChild<SliderSpinBox*>(QStringLiteral("regionPaddingLeft"));
    auto* rectangle = panel.findChild<QPushButton*>(QStringLiteral("createRegionRectangle"));
    auto* ellipse = panel.findChild<QPushButton*>(QStringLiteral("createRegionEllipse"));
    auto* custom = panel.findChild<QPushButton*>(QStringLiteral("createRegionCustom"));
    auto* edit = panel.findChild<QPushButton*>(QStringLiteral("editRegion"));
    auto* editHole = panel.findChild<QPushButton*>(QStringLiteral("editRegionHole"));
    auto* hole = panel.findChild<QPushButton*>(QStringLiteral("addRegionHole"));
    QVERIFY(mode && horizontal && vertical && overflow && padding);
    QVERIFY(rectangle && ellipse && custom && edit && editHole && hole);

    QSignalSpy modeSpy(&panel, &TypographyPanel::typographyLayoutModeChanged);
    QSignalSpy paddingSpy(&panel, &TypographyPanel::regionPaddingChanged);
    QSignalSpy rectangleSpy(&panel, &TypographyPanel::createRegionRectangleRequested);
    mode->setCurrentIndex(mode->findData(static_cast<int>(TypographyLayoutMode::Region)));
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(modeSpy.at(0).at(0).toInt(), static_cast<int>(TypographyLayoutMode::Region));
    padding->spinBox()->setValue(24.0);
    QCOMPARE(paddingSpy.count(), 1);
    QCOMPARE(paddingSpy.at(0).at(0).toInt(), static_cast<int>(RegionPaddingSide::Left));
    QCOMPARE(paddingSpy.at(0).at(1).toDouble(), 24.0);
    rectangle->click();
    QCOMPARE(rectangleSpy.count(), 1);

    TextObject object;
    object.region = TypographyRegion::makeRectangle(QRectF(0.0, 0.0, 200.0, 100.0));
    const TypographyRegion hole = TypographyRegion::makeRectangle(QRectF(40.0, 20.0, 120.0, 60.0));
    object.region->holes.push_back(hole.outer);
    object.layoutMode = TypographyLayoutMode::Region;
    object.regionLayout.regionId = object.region->id;
    object.regionLayout.paddingLeft = 31.0;
    object.regionLayout.horizontalAlignment = RegionHorizontalAlignment::Right;
    object.regionLayout.verticalAlignment = RegionVerticalAlignment::Bottom;
    panel.refresh(object);
    QCOMPARE(padding->value(), 31.0);
    QCOMPARE(horizontal->currentData().toInt(), static_cast<int>(RegionHorizontalAlignment::Right));
    QCOMPARE(vertical->currentData().toInt(), static_cast<int>(RegionVerticalAlignment::Bottom));
    QCOMPARE(overflow->currentData().toInt(), static_cast<int>(RegionOverflowMode::Clip));
    QVERIFY(edit->isEnabled());
    QVERIFY(editHole->isEnabled());
    QVERIFY(hole->isEnabled());
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);
    RegionTypographyUiTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "region_typography_ui_tests.moc"
