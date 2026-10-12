// The pxl.h of the playground. The host of the playground has the
// implementation of pxl.h, so this file hides PXL_IMPLEMENTATION from the
// real header (pxl/pxl.h). Programs compile faster without it.

#ifdef PXL_IMPLEMENTATION
#undef PXL_IMPLEMENTATION
#include <pxl/pxl.h>
#define PXL_IMPLEMENTATION
#else
#include <pxl/pxl.h>
#endif
