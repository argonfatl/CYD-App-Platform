# CYD App SDK

The SDK contract is intentionally not finalized until the ELF runtime prototype has been validated.

The first version should provide a small lifecycle and service API rather than exposing internal platform objects. Planned concepts include:

- app identity and SDK/API version;
- initialization and cleanup;
- cooperative update/tick callback;
- request to return to the launcher;
- display and input access through platform-owned services;
- app-specific persistent data access.

The platform must own shared hardware and coordinate cleanup before switching apps.
