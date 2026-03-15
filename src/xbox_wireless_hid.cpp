#include "xbox_wireless_hid.h"
#include <algorithm>
#include <windows.h>
#include <winternl.h>
#include <hidusage.h>
#include <hidpi.h>

bool XboxWireless_IsDevice(uint16_t vendorId, uint16_t productId)
{
	return vendorId == XboxWireless_VendorId && productId == XboxWireless_ProductId;
}

void XboxWireless_ApplyRightTrigger(uint16_t vendorId, uint16_t productId, bool sony,
                                    GamepadState& gs, void* pp,
                                    const char* report, unsigned long rLen, unsigned short numLinkCollectionNodes)
{
	if (sony || !XboxWireless_IsDevice(vendorId, productId))
		return;

	auto* preparsed = static_cast<PHIDP_PREPARSED_DATA>(pp);
	ULONG rzVal = 0;
	NTSTATUS rzStatus = 1;
	const USHORT maxLink = (numLinkCollectionNodes > 0) ? numLinkCollectionNodes : 1;
	for (USHORT lc = 0; lc < maxLink && lc < 32u; ++lc)
	{
		rzStatus = HidP_GetUsageValue(HidP_Input, HID_USAGE_PAGE_GENERIC, lc, HID_USAGE_GENERIC_RZ,
		                              &rzVal, preparsed, const_cast<char*>(report), rLen);
		if (rzStatus == HIDP_STATUS_SUCCESS)
		{
			// Same as NormTrigger fallback for invalid range (centered 16-bit).
			gs.rightTrigger = std::clamp((static_cast<float>(rzVal) - 32768.0f) / 32767.0f, 0.0f, 1.0f);
			return;
		}
	}

	if (rLen >= 11)
	{
		const size_t dataStart = (rLen > 0) ? 1u : 0u;
		const size_t triggerWordOffset = dataStart + 8u;
		uint8_t combined = static_cast<unsigned char>(report[triggerWordOffset + 1]);
		if (combined <= 128u)
			gs.rightTrigger = std::clamp((128.0f - static_cast<float>(combined)) / 128.0f, 0.0f, 1.0f);
		else
			gs.rightTrigger = 0.0f;
	}
}
