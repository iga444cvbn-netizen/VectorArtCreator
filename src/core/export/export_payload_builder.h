#pragma once
#include "core/document/document.h"
#include "core/evaluation/work_control.h"
#include "core/export/vector_export_payload.h"
#include "core/scene/scene_geometry.h"
#include <QStringList>
namespace vt { class ExportPayloadBuilder final { public: [[nodiscard]] static bool build(const Document&,const Page&,const SceneGeometry&,ExportScope,const QStringList&,VectorExportPayload*,QString*,const WorkControl& work = WorkControl::withBudget()); }; }
