/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_REFILL_H
#define HDMI_AHI_REFILL_H
#include "pacing.h"
/* Observed ownership is refreshed even when a refill has to wait. After a
 * missed poll, the same CB may mean a whole lap has elapsed. Mark a refill
 * pending in that case too; never infer the writable half from elapsed time. */
struct ahi_refill {
    int active;
    unsigned pending, resync;
};
static inline void ahi_refill_defer(struct ahi_refill *r,struct pcm_pacer *p,
                                    const struct pcm_cursor *c,uint32_t now)
{
    if(!r->resync) pcm_pacer_rebase(p,c,now);
    r->resync=1;
}
static inline void ahi_refill_observe(struct ahi_refill *r,struct pcm_pacer *p,
                                      const struct pcm_cursor *c,int current,
                                      unsigned late,uint32_t now)
{
    if(current<0) return;
    if(current!=r->active || late) r->pending=1;
    r->active=current;
    if(late) {
        pcm_pacer_rebase(p,c,now);
        r->resync=1;
    }
}
static inline void ahi_refill_published(struct ahi_refill *r,struct pcm_pacer *p,
                                        const struct pcm_cursor *c,uint32_t now)
{
    if(r->resync) pcm_pacer_rebase(p,c,now);
    p->filled=0;
    r->pending=r->resync=0;
}
#endif
