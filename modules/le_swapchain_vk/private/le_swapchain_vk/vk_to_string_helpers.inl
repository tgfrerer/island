#pragma once

// ----------------------------------------------------------------------

static constexpr char const* to_str( const VkPresentModeKHR& tp ) {
	switch ( static_cast<uint32_t>( tp ) ) {
		// clang-format off
	case          0: return "VkPresentModeImmediateKhr";
	case          1: return "VkPresentModeMailboxKhr";
	case          2: return "VkPresentModeFifoKhr";
	case          3: return "VkPresentModeFifoRelaxedKhr";
	case 1000111000: return "VkPresentModeSharedDemandRefreshKhr";
	case 1000111001: return "VkPresentModeSharedContinuousRefreshKhr";
	default: return "Unknown";
		// clang-format on
	};
}
static constexpr char const* to_str( const VkResult& tp ) {
	switch ( static_cast<int32_t>( tp ) ) {
		// clang-format off
	case         -1: return "VkErrorOutOfHostMemory";
	case        -10: return "VkErrorTooManyObjects";
	case -1000000000: return "VkErrorSurfaceLostKhr";
	case -1000000001: return "VkErrorNativeWindowInUseKhr";
	case -1000001004: return "VkErrorOutOfDateKhr";
	case -1000003001: return "VkErrorIncompatibleDisplayKhr";
	case -1000011001: return "VkErrorValidationFailedExt";
	case -1000012000: return "VkErrorInvalidShaderNv";
	case -1000069000: return "VkErrorOutOfPoolMemory";
	case -1000072003: return "VkErrorInvalidExternalHandle";
	case -1000158000: return "VkErrorInvalidDrmFormatModifierPlaneLayoutExt";
	case -1000161000: return "VkErrorFragmentation";
	case -1000174001: return "VkErrorNotPermittedKhr";
	case -1000255000: return "VkErrorFullScreenExclusiveModeLostExt";
	case -1000257000: return "VkErrorInvalidOpaqueCaptureAddress";
	case        -11: return "VkErrorFormatNotSupported";
	case        -12: return "VkErrorFragmentedPool";
	case        -13: return "VkErrorUnknown";
	case         -2: return "VkErrorOutOfDeviceMemory";
	case         -3: return "VkErrorInitializationFailed";
	case         -4: return "VkErrorDeviceLost";
	case         -5: return "VkErrorMemoryMapFailed";
	case         -6: return "VkErrorLayerNotPresent";
	case         -7: return "VkErrorExtensionNotPresent";
	case         -8: return "VkErrorFeatureNotPresent";
	case         -9: return "VkErrorIncompatibleDriver";
	case          0: return "VkSuccess";
	case          1: return "VkNotReady";
	case          2: return "VkTimeout";
	case          3: return "VkEventSet";
	case          4: return "VkEventReset";
	case          5: return "VkIncomplete";
	case 1000001003: return "VkSuboptimalKhr";
	case 1000268000: return "VkThreadIdleKhr";
	case 1000268001: return "VkThreadDoneKhr";
	case 1000268002: return "VkOperationDeferredKhr";
	case 1000268003: return "VkOperationNotDeferredKhr";
	case 1000297000: return "VkPipelineCompileRequired";
	default: return "Unknown";
		// clang-format on
	};
}
