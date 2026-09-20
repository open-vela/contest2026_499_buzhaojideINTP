/****************************************************************************
 * vendor/allwinner/chips/f1c100s/bootctl.h
 *
 * AP-side boot control API. Names match openvela bootctl.h
 * (bootctl_active/update/done/success). Storage is the F1C100s KV
 * sector, not openvela's property/kvdb + A/B slots.
 *
 * Spec §7: signature verification can be stacked later without
 * changing these four call sites.
 ****************************************************************************/

#ifndef __F1C100S_BOOTCTL_H
#define __F1C100S_BOOTCTL_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Single AP slot name. No A/B on this board. */

#define BOOTCTL_SLOT_AP "ap"

const char *bootctl_active(void);
int bootctl_update(void);
int bootctl_done(void);
int bootctl_success(void);

#ifdef __cplusplus
}
#endif

#endif /* __F1C100S_BOOTCTL_H */
