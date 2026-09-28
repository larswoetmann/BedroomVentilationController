# CTS400 mode and immediate settings

The Ventilation Configuration stores fan levels 1–4 and RTS separately for Day Mode and Night Mode, applying the active set at startup and mode changes. Other verified CTS400 settings are persisted and applied individually; this deliberately replaces direct per-value mode writes so scheduled ventilation remains the source of truth while still allowing safe adjustment of non-mode settings.
