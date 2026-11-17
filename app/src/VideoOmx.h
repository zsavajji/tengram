#ifndef VIDEOOMX_H
#define VIDEOOMX_H

// Small helpers VideoDecoder's two source files share.


#include "VideoDecoder.h"

#include <string.h>
#include <time.h>
#include <OMX_Core.h>

// The most buffers taken on either port, whatever the component asks for.
static const int MaxBuffers = 40;

static inline double nowMs()
{
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static inline double cpuMs()
{
    timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static inline QByteArray errName(OMX_ERRORTYPE e)
{
    switch (unsigned(e)) {
#define E(x) case OMX_Error##x: return "OMX_Error" #x;
    E(None) E(InsufficientResources) E(Undefined) E(InvalidComponentName) E(ComponentNotFound)
    E(InvalidComponent) E(BadParameter) E(NotImplemented) E(Underflow) E(Overflow) E(Hardware)
    E(InvalidState) E(StreamCorrupt) E(PortsNotCompatible) E(ResourcesLost) E(NoMore)
    E(VersionMismatch) E(NotReady) E(Timeout) E(SameState) E(ResourcesPreempted)
    E(PortUnresponsiveDuringAllocation) E(PortUnresponsiveDuringDeallocation)
    E(PortUnresponsiveDuringStop) E(IncorrectStateTransition) E(IncorrectStateOperation)
    E(UnsupportedSetting) E(UnsupportedIndex) E(BadPortIndex) E(PortUnpopulated)
    E(ComponentSuspended) E(DynamicResourcesUnavailable) E(MbErrorsInFrame) E(FormatNotDetected)
    E(ContentPipeOpenFailed) E(ContentPipeCreationFailed) E(SeperateTablesUsed)
    E(TunnelingUnsupported)
#undef E
    }
    return "0x" + QByteArray::number(unsigned(e), 16);
}

template <class T> static inline void initStruct(T &x)
{
    memset(&x, 0, sizeof x);
    x.nSize = sizeof x;
    x.nVersion.s.nVersionMajor = 1;
    x.nVersion.s.nVersionMinor = 1;
    x.nVersion.s.nRevision = 2;
}

#endif
