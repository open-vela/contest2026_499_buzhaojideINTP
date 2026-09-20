/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_oneshot.h
 *
 * F1C100s Oneshot Timer Internal Header
 ****************************************************************************/

#ifndef __F1C100S_ONESHOT_H
#define __F1C100S_ONESHOT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/timers/oneshot.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

struct oneshot_lowerhalf_s *f1c100s_oneshot_initialize(void);

#endif /* __F1C100S_ONESHOT_H */
