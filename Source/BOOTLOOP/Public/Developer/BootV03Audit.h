#pragma once
class ABootGameMode;
#if !UE_BUILD_SHIPPING
/** Opt-in rendered native audit; excluded from null-RHI automation and Shipping. */
void StartBootV03Audit(ABootGameMode* Game);
#endif
