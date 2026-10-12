#include "cs/Object.h"

namespace cs {

namespace {
Heap gDefaultHeap;
Heap* gCurrent = &gDefaultHeap;
} // namespace

Heap& CurrentHeap() { return *gCurrent; }

void SetCurrentHeap(Heap* heap) { gCurrent = heap ? heap : &gDefaultHeap; }

} // namespace cs
