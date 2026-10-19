# Architecture (draft)

## Platform responsibilities

- **Core:** board setup, shared hardware services, app lifecycle, error handling.
- **Launcher:** app list, launch/exit, settings, and recovery UI.
- **App SDK:** versioned API exposed to compatible apps.
- **App Library:** installed app metadata, version, source repository, and trust status.
- **Web Manager:** repository registration, install/update/remove actions, and status reporting.
- **Package tooling:** validates manifests and creates target-specific prebuilt packages.

## App storage (proposed)

Each app gets a stable ID and its own directory. For example:

```text
/apps/<app-id>/
  manifest.json
  app.elf
  assets/
  data/
```

The exact filesystem and path layout will be finalized after flash/SD capacity and runtime choices are validated. User data must be kept separate from replaceable program files.

## App sources and trust

A repository can provide metadata and release artifacts, but a repository must not be able to declare itself officially approved. Official approval must come from trusted platform metadata. User-added repositories should be visibly marked as unreviewed.

Downloaded packages should be validated before installation. Failed updates must not destroy the currently working app or its user data.

## Runtime

The current leading candidate is Espressif ELF Loader under ESP-IDF. This remains provisional until the hardware prototype succeeds. Apps should use platform APIs rather than directly owning shared display or touch hardware.
