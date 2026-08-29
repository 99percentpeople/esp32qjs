# Production Security and Update Plan

Status: design and qualification plan. No production security eFuse is enabled
by the framework or development Build Contexts.

Secure Boot, flash encryption, debug-port lockdown, and anti-rollback change
irreversible device state. They belong to a product manufacturing profile and
provisioning service, not to the generic firmware default or a browser-provided
build command. Development boards must remain recoverable until the complete
provisioning and update path has passed on sacrificial fixtures.

This plan follows Espressif's target-specific guidance for
[platform security](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/security.html),
[combined security enablement](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c5/security/security-features-enablement-workflows.html),
and [OTA rollback and anti-rollback](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html).
The exact eFuse names and supported signature schemes must be rechecked against
the pinned ESP-IDF release and silicon revision before manufacturing approval.

## Trust boundary

- The Bun host/control plane authenticates operators, selects an allowlisted
  Build Context, authorizes releases, and never sends provider credentials to
  the device.
- The signing service owns firmware and resource-artifact signing keys. Private
  keys do not enter source control, CI logs, Build Contexts, device filesystems,
  or the browser.
- Each device receives a unique identity and unique encryption material where
  the selected ESP-IDF workflow permits host provisioning. Provisioning output
  is recorded against the device serial number without logging key material.
- The device trusts only authenticated firmware and resource manifests. Product
  pairing credentials and application secrets use encrypted NVS; ordinary
  LittleFS content is not treated as a secret store merely because flash
  encryption is enabled.

## Production profile

For each supported production MCU and hardware revision, define one reviewed
Build Context that includes:

1. Secure Boot v2 with a documented signing scheme and at least one tested key
   rotation path. ESP32-C5 must use the scheme allowed by the current silicon
   errata and pinned ESP-IDF release; do not copy S3 settings blindly.
2. Flash encryption in release mode with a unique per-device key and the
   product-approved UART download policy.
3. NVS encryption with a dedicated NVS key partition or target-supported HMAC
   scheme, covering Wi-Fi credentials, device identity, and application secrets.
4. A two-slot OTA partition table plus `otadata`, signed application images,
   rollback confirmation after health checks, and an explicit recovery image or
   factory-service procedure.
5. Disabled or restricted JTAG, USB-JTAG, direct boot, and ROM download
   interfaces according to the physical-service threat model.
6. A monotonically increasing application security version for vulnerability
   releases. Semantic application versions remain independent from this
   anti-rollback counter.

## Artifact pipeline

The release service, not the device or browser, produces an immutable manifest
containing the target, board/Build Context digest, ESP-IDF revision, firmware
digest, resource-image digest, application version, security version, signing
key identifier, minimum compatible bootloader, and release timestamp. Sign the
exact distributable binaries after reproducible CI builds and publish the
manifest and artifacts atomically.

OTA download uses authenticated TLS with normal hostname, CA-chain, and date
verification. The device verifies the signed image before selection. On first
boot of a pending image, application health checks must cover required
filesystems, NVS, runtime startup, and the product control channel before calling
the ESP-IDF API that marks the image valid. A reset, watchdog, panic, or missed
health deadline leaves the image eligible for rollback.

Resource/workspace updates need their own signed manifest and atomic staging;
successful firmware verification does not authenticate separately downloaded
JavaScript or data. An uncertain transport result is audited by digest/version
before retry and never treated as confirmed installation.

## Provisioning stages

1. **Offline dry run:** generate disposable keys, build signed/encrypted
   artifacts, inspect partition offsets and image sizes, and verify no private
   material enters build logs or artifacts.
2. **Sacrificial development units:** exercise first boot, power interruption,
   invalid signatures, wrong keys, corrupt NVS, rollback, recovery, and all
   intended service interfaces before burning final lockdown eFuses.
3. **Pilot manufacturing:** provision a small serialized batch with audited
   operator authorization, uninterrupted power, read-back of non-secret eFuse
   state, and a per-device pass/fail record.
4. **Field update rehearsal:** upgrade, fail health checks, roll back, rotate a
   signing key, reject a revoked/old security version, and recover a device using
   only the interfaces that remain available in production.
5. **Production approval:** freeze target-specific settings and tooling only
   after the pilot evidence is reviewed. Keep development and production
   profiles separate thereafter.

## Acceptance evidence

- Secure Boot and flash encryption state verified after cold boot on each
  supported production target and silicon revision.
- Plaintext, unsigned, wrongly signed, corrupted, and downgraded images rejected
  as designed.
- Valid OTA accepted only after health confirmation; power loss at each update
  phase either preserves the old image or rolls back.
- Encrypted NVS survives expected updates and rejects missing/wrong key state
  without silently replacing identity or secrets.
- Signing-key rotation and revocation demonstrated without losing every recovery
  path.
- Manufacturing logs contain artifact/key identifiers and eFuse summaries, but
  no private keys, flash-encryption keys, NVS keys, Wi-Fi credentials, or device
  secrets.

Production enablement is complete only when these checks have device-backed
records. A secure build alone is not proof that irreversible provisioning or
field recovery works.
