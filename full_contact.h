#pragma once

#include <stdint.h>

// Full-throttle contact FULL on the power board (reference Power V1.0, GPIO43: LOW = close).
void fullContactBegin();
// Control task, once per cycle. Opens immediately when trigger or output leave 100 %.
void fullContactUpdate(bool enabled, bool triggerFull, bool outputFull, uint32_t vinMilliVolts);
bool fullContactRequested();
bool fullContactEngaged();
bool fullContactOvercurrent();
// False while the contact is requested and for a release time after opening: no lowside then.
bool fullContactLowSideAllowed();
// Flash writes stall the control task: keep the contact open meanwhile.
void fullContactInhibit(bool inhibit);

struct FullContactWriteGuard {
  FullContactWriteGuard() { fullContactInhibit(true); }
  ~FullContactWriteGuard() { fullContactInhibit(false); }
};
