/* Copyright (C) 2007 Jean-Marc Valin
 *
 * Minimal OS helpers used by RNNoise's vector kernels (from Opus/CELT).
 * The RNNoise 0.2 release tarball omits this file; ARM/NEON and the scalar
 * path both include it.
 */
#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H

#include <string.h>
#include <stdlib.h>

#include "opus_types.h"

#ifndef OVERRIDE_OPUS_COPY
#define OPUS_COPY(dst, src, n) (memcpy((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))
#endif

#ifndef OVERRIDE_OPUS_MOVE
#define OPUS_MOVE(dst, src, n) (memmove((dst), (src), (n)*sizeof(*(dst)) + 0*((dst)-(src)) ))
#endif

#ifndef OVERRIDE_OPUS_CLEAR
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n)*sizeof(*(dst))))
#endif

#endif /* OS_SUPPORT_H */
