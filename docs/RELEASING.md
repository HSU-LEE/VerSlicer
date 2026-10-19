# Releasing VerSlicer

## One-time setup

1. **Apple Developer Program** ($99/year) → create a *Developer ID Application* certificate and export it as a `.p12`.
2. Create an app-specific password for your Apple ID (appleid.apple.com → Sign-In and Security).
3. Add these repository secrets (GitHub → Settings → Secrets and variables → Actions):

| Secret | Value |
| --- | --- |
| `APPLE_CERT_P12_BASE64` | `base64 -i cert.p12` output |
| `APPLE_CERT_PASSWORD` | password of the `.p12` |
| `APPLE_SIGNING_IDENTITY` | `Developer ID Application: Your Name (TEAMID)` |
| `APPLE_ID` | Apple ID email |
| `APPLE_TEAM_ID` | 10-character team ID |
| `APPLE_APP_PASSWORD` | app-specific password |

Without them, the release workflow still builds a DMG, but it is ad-hoc signed: users must right-click → **Open** the first time.

## Each release

1. Set `VERSLICER_VERSION` in `version.inc` (semver, e.g. `3.2.1`).
2. Add a `## 3.2.1` section at the top of `CHANGELOG.md`.
3. Run the checks locally:
   ```bash
   ./scripts/run_orca_tools_test.sh
   ```
4. Go through `docs/QA_CHECKLIST.md` on a Release build.
5. Commit, then tag and push:
   ```bash
   git tag v3.2.1 && git push origin main v3.2.1
   ```
6. `.github/workflows/release.yml` builds, signs, notarizes and attaches the DMG to a **draft** release. Review it on GitHub and publish.

The app checks `api.github.com/repos/HSU-LEE/VerSlicer/releases` and offers the newest published, non-draft release whose tag is a higher version. Tags must look like `v<version>`; the workflow refuses a tag that does not match `version.inc`.

## Source availability (AGPL-3.0)

Every published DMG must have its exact source available. Tag the commit you build from and keep the tag public.
