/* Recto's MuPDF feature configuration.
 *
 * MuPDF: pinned release 1.28.5, commit 8ad45e92f0935d3d87f1db3f873086472a5e1b24
 * (git submodule at third_party/mupdf; pin record in
 * todo/00-workspace/TODO-01-repo-and-build.md section 2 and NOTICE.md).
 *
 * MuPDF consumes feature choices as preprocessor defines read by
 * include/mupdf/fitz/config.h before its #ifndef defaults. This header is
 * that define set, kept in one versioned file so every build of MuPDF in
 * Recto is configured identically. Wire it as a forced include ahead of
 * every MuPDF translation unit (MSVC: /FI build\mupdf-config.h, applied
 * through the CL environment variable when building the upstream win32
 * projects; the Recto solution in D00 T01 section 3 imports it the same
 * way). Its companion build/mupdf-win32.targets removes the MuJS unity
 * file the upstream win32 libthirdparty project would otherwise always
 * compile. Do not edit config choices into the submodule: the pin must
 * stay reproducible from the upstream tag plus these Recto files.
 *
 * Decisions (docs/plans/2026-10-13-recto-foundation.md): D9 (own config,
 * not the vcpkg port or generated bindings), D12 (JavaScript disabled).
 */

#ifndef RECTO_MUPDF_CONFIG_H
#define RECTO_MUPDF_CONFIG_H

/* D12: the MuJS JavaScript interpreter is out. PDF JavaScript never runs in
 * Recto, so the interpreter is never built: the thirdparty/mujs submodule
 * stays uninitialized, and the build must succeed without it. */
#define FZ_ENABLE_JS 0

/* Everything else keeps the upstream 1.28.5 defaults from
 * include/mupdf/fitz/config.h, deliberately:
 * - document handlers: all (PDF plus the image and ebook formats the
 *   writers and open paths rely on);
 * - document writers: all, including DOCX and ODT output (plan section 5);
 * - ICC profiles, JPEG2000 and Brotli: enabled (rendering correctness);
 * - fonts: the upstream set (base 14, CJK fallback, Charis SIL). The
 *   foundation plan names "font choices" without choosing one, so none is
 *   invented here; trimming fonts later is a dated plan correction. */

#endif /* RECTO_MUPDF_CONFIG_H */
