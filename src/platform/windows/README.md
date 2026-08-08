# Windows platform boundary

Windows-native functionality is intentionally absent from the first vertical slice.
Future EMF export and clipboard integration belong here behind the portable export and
platform service interfaces. Core document, shaping, geometry, effects, serialization,
and canvas code must not include Win32 headers or Windows filesystem assumptions.
