/* Recto's C bridge to MuPDF: the only Recto module that may include MuPDF
 * headers (foundation plan 3.1, 4.4). Everything above the bridge sees
 * Recto's own types through this header, so MuPDF's setjmp/longjmp error
 * handling can never unwind through C++ frames.
 *
 * Scaffold state (D00 T01 section 3): the ABI version entry point only.
 * The runtime, error bridge and document API land in D01 T01. */
#ifndef RECTO_BRIDGE_H
#define RECTO_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Bridge ABI version. Bumped whenever this header changes incompatibly. */
int rc_bridge_abi_version(void);

#ifdef __cplusplus
}
#endif

#endif /* RECTO_BRIDGE_H */
