// sr3agora/agora_backend.h implementation - NullAgoraBackend. See the
// header's own doc comment for the contract this satisfies
// (spec-agora-hydra-boundary.md Sec2/Sec4).
#include "sr3agora/agora_backend.h"

namespace sr3agora {

AgoraClientIdentity NullAgoraBackend::deriveClientIdentity() {
    log("Hydra::DeriveClientIdentity(0x004023a0)");  // Sec2.2 step3
    return identity_;
}

bool NullAgoraBackend::initFailureHelperA() {
    log("Hydra::InitFailureHelperA(0x00402880)");  // Sec2.2 step4
    return forceFailureA_;
}

bool NullAgoraBackend::initFailureHelperB() {
    log("Hydra::InitFailureHelperB(0x004018b0)");  // Sec2.2 step5
    return forceFailureB_;
}

uint64_t NullAgoraBackend::nextSequenceNumber() {
    log("Hydra::NextSequenceNumber(InterlockedExchangeAdd, 0x013521c8)");  // Sec4.1 step1
    return nextSequence_++;
}

void NullAgoraBackend::recordSubmittedReport(const AgoraValueArray& fields) {
    log("Hydra::HandOffToTransport(0x00401b30)");  // Sec4.1 step6 success path
    submittedReports_.push_back(fields);
}

}  // namespace sr3agora
