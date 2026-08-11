# Windows platform boundary

`WindowsVectorClipboardService` is the sole Win32/GDI+ boundary for production
clipboard output. Core first creates a portable `VectorExportPayload` at 96 logical
DPI; this layer records EMF+ Dual and owns every `HENHMETAFILE`/`HGLOBAL` until a
successful `SetClipboardData` transfer. It publishes EMF, SVG, PNG and Unicode
text, while the editor's Ctrl+C object clipboard remains separate.

No Windows headers are permitted in document, shaping, geometry, effects, canvas,
serialization, or portable export code.
