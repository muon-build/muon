#ifdef TRACY_ENABLE
#include "TracyC.h"

#define TracyCZoneAutoS TracyCZoneN(tctx_func, __func__, true)
#define TracyCZoneAutoE TracyCZoneEnd(tctx_func)

#else

#define TracyCZoneAutoS
#define TracyCZoneAutoE

#define TracyCZone(c, x)
#define TracyCZoneN(c, x, y)
#define TracyCZoneC(c, x, y)
#define TracyCZoneNC(c, x, y, z)
#define TracyCZoneEnd(c)
#define TracyCZoneText(c, x, y)
#define TracyCZoneName(c, x, y)
#define TracyCZoneValue(c, x)

#define TracyCAlloc(x, y)
#define TracyCFree(x)
#define TracyCSecureAlloc(x, y)
#define TracyCSecureFree(x)

#define TracyCFrameMark
#define TracyCFrameMarkNamed(x)
#define TracyCFrameMarkStart(x)
#define TracyCFrameMarkEnd(x)
#define TracyCFrameImage(x, y, z, w, a)

#define TracyCPlot(x, y)
#define TracyCMessage(x, y)
#define TracyCMessageL(log_misc, x)
#define TracyCMessageC(x, y, z)
#define TracyCMessageLC(x, y)
#define TracyCAppInfo(x, y)

#define TracyCZoneS(x, y, z)
#define TracyCZoneNS(x, y, z, w)
#define TracyCZoneCS(x, y, z, w)
#define TracyCZoneNCS(x, y, z, w, a)

#endif
