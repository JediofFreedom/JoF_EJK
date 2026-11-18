#ifndef EJK_BG_ANIM_EVENTS_H
#define EJK_BG_ANIM_EVENTS_H

#include <stdint.h>
#include <string.h>

// Included after bg_public.h. Only keys/slots are indexed: deferred effect
// bolt resolution and per-actor sound data remain in the original event array.
static inline void BG_BuildAnimEventIndex(bgAnimEventIndex_t *index,const animevent_t *events)
{
    int count = 0,i;
    index->valid = qfalse;
    while (count < MAX_ANIM_EVENTS && events[count].eventType != AEV_NONE) {
        int slot = count;
        // Stable source-slot order for equal keys; sorting occurs only at load.
        while (slot > 0 && events[index->byFrame[slot-1]].keyFrame > events[count].keyFrame) {
            index->byFrame[slot] = index->byFrame[slot-1];
            --slot;
        }
        index->byFrame[slot] = (unsigned short)count;
        ++count;
    }
    index->count = (unsigned short)count;
    for (i = count; i < MAX_ANIM_EVENTS; ++i) index->byFrame[i] = 0;
    index->valid = qtrue;
}

static inline void BG_InvalidateAnimEventIndex(const animevent_t *events)
{
    int i;
    for (i = 0; i < MAX_ANIM_FILES; ++i) {
        if (events == bgAllEvents[i].torsoAnimEvents) bgAllEvents[i].torsoIndex.valid = qfalse;
        if (events == bgAllEvents[i].legsAnimEvents) bgAllEvents[i].legsIndex.valid = qfalse;
    }
}

static inline void BG_BuildAnimEventIndexes(bgLoadedEvents_t *events)
{
    BG_BuildAnimEventIndex(&events->torsoIndex,events->torsoAnimEvents);
    BG_BuildAnimEventIndex(&events->legsIndex,events->legsAnimEvents);
}

typedef struct {
    uint32_t slots[(MAX_ANIM_EVENTS+31)/32];
    int word;
} bgAnimEventCandidates_t;

static inline void BG_AnimEventCandidateRange(bgAnimEventCandidates_t *query,
    const bgAnimEventIndex_t *index,const animevent_t *events,int64_t first,int64_t last)
{
    int low = 0,high = index->count;
    // Binary search only the key range; original event slots determine dispatch
    // order later. Bounds use 64 bits so frame +/- 3 cannot overflow signed int.
    while (low < high) {
        int middle = low+(high-low)/2;
        if (events[index->byFrame[middle]].keyFrame < first) low = middle+1;
        else high = middle;
    }
    while (low < index->count) {
        unsigned slot = index->byFrame[low++];
        if (events[slot].keyFrame > last) break;
        query->slots[slot/32] |= UINT32_C(1) << (slot%32);
    }
}

static inline void BG_AnimEventCandidatesInit(bgAnimEventCandidates_t *query,
    const bgAnimEventIndex_t *index,const animevent_t *events,int oldFrame,int frame,qboolean nearby)
{
    memset(query,0,sizeof(*query));
    if (nearby) {
        BG_AnimEventCandidateRange(query,index,events,(int64_t)oldFrame-3,(int64_t)oldFrame+3);
        BG_AnimEventCandidateRange(query,index,events,(int64_t)frame-3,(int64_t)frame+3);
    } else {
        BG_AnimEventCandidateRange(query,index,events,frame,frame);
    }
}

static inline int BG_AnimEventCandidateNext(bgAnimEventCandidates_t *query)
{
    while (query->word < (MAX_ANIM_EVENTS+31)/32) {
        uint32_t bits = query->slots[query->word],shifted;
        int bit = 0;
        if (!bits) { ++query->word; continue; }
        // Portable trailing-zero search, bounded by five integer comparisons.
        shifted = bits;
        if (!(shifted & UINT32_C(0xffff))) { shifted >>= 16; bit += 16; }
        if (!(shifted & UINT32_C(0xff))) { shifted >>= 8; bit += 8; }
        if (!(shifted & UINT32_C(0xf))) { shifted >>= 4; bit += 4; }
        if (!(shifted & UINT32_C(3))) { shifted >>= 2; bit += 2; }
        if (!(shifted & UINT32_C(1))) ++bit;
        query->slots[query->word] = bits & (bits-1);
        return query->word*32+bit;
    }
    return MAX_ANIM_EVENTS;
}
#endif
