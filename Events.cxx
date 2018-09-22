#include "sys.h"
#include "debug.h"
#include "Events.h"

namespace event {

void BusyInterface::flush_events()
{
#ifdef CWDEBUG
  if (m_busy_depth != 1)
    DoutFatal(dc::core, "Expected `m_busy_depth' to be 1 in flush_events()");
#endif
  do
  {
    m_events.front()->retrigger();
    m_events.pop_front();
  }
  while (m_busy_depth == 1 && !m_events.empty());
}

BusyInterface dummy_busy_interface;

} // namespace event
