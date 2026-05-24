#include "hi_type.h"

extern "C" {

// libmpi.a references these audio symbols internally even when this product
// never calls audio APIs. Keep them as failing weak stubs so live_stream does
// not link the real VoiceEngine/VQE libraries or expose audio support.
HI_S32 __attribute__((weak)) HI_VOICE_EncReset(...) { return HI_FAILURE; }

HI_S32 __attribute__((weak)) HI_VOICE_DecReset(...) { return HI_FAILURE; }

HI_S32 __attribute__((weak)) HI_VOICE_EncodeFrame(...) { return HI_FAILURE; }

HI_S32 __attribute__((weak)) HI_VOICE_DecodeFrame(...) { return HI_FAILURE; }

HI_S32 __attribute__((weak)) HI_DNVQE_WriteFrame(...) { return HI_FAILURE; }

HI_S32 __attribute__((weak)) HI_DNVQE_ReadFrame(...) { return HI_FAILURE; }

}
