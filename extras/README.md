# Charging ribbon source

`PowerRibbon.cpp` is the original standalone 3-second Windows charging indicator.
It uses green for `pd`, a moving rainbow for `barrel`, and opens without focus.
It reads the Windows battery percentage and hides when Windows reports AC absent.
`Start-ChargingIndicator.ps1` launches it after a verified charger transition.
No third-party tuning applications or personal presets are included here.
