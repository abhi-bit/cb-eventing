#ifndef __EVENT_ASSERT_H
#define __EVENT_ASSERT_H

#ifdef CBASSERT_ENABLED
#include <platform/cbassert.h>

inline void assert(bool flag)
{
    cb_assert(flag);
}
#endif //CBASSERT_ENABLED
#endif //__EVENT_ASSERT_H
