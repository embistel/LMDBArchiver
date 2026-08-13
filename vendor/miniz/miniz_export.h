/* miniz_export.h - static-vendor shim.
   The upstream miniz_export.h is CMake-generated for shared-library symbol visibility.
   We build miniz as a private static library, so MINIZ_EXPORT is a no-op. */
#ifndef MINIZ_EXPORT_H
#define MINIZ_EXPORT_H
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
#endif
