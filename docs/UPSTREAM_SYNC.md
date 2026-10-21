# Keeping up with OrcaSlicer

VerSlicer is OrcaSlicer plus a VerSlicer layer (AI assistant, Smart Print, MakerWorld, Bambu Connect). OrcaSlicer keeps shipping new printer profiles and fixes; this guide is how we pull them in without losing our layer.

## What is ours

Run `./scripts/verslicer_hooks.sh` for the current list.

| Kind | Where | On an upstream merge |
| --- | --- | --- |
| VerSlicer folders | `src/slic3r/GUI/Assistant`, `BambuSmartPrint`, `MakerWorld`, `AICoach`, `AIPipeline`, `ModelSearch`, `src/libslic3r/BambuSmartPrint`, `BambuConnect*` | Never touched by OrcaSlicer: keep ours |
| Hook points | OrcaSlicer files that include VerSlicer code (Plater, GUI_App, MainFrame, GLCanvas3D, Monitor, Preferences, …) | Merge by hand: take theirs, re-apply our calls |
| Branding | `version.inc`, `src/dev-utils/platform/*`, resources/images `Verslicer*`, display strings "VerSlicer" | Keep ours |
| Korean catalog | `localization/i18n/ko/OrcaSlicer_ko.po` | Merge msgids; keep our translations |

Rules for new VerSlicer code, so the hook list stays short:

1. Put new features in a VerSlicer folder, not in an OrcaSlicer file.
2. When an OrcaSlicer file must call us, add one call to a function in our folder (one `#include`, one line), and mark it `// VerSlicer:`.
3. Do not edit OrcaSlicer behaviour in place when a hook can do it.

## One-time setup: a merge base

The repository history starts at "Initial VerSlicer release", which imported OrcaSlicer without its history, so Git cannot merge OrcaSlicer releases directly. Once:

```bash
git remote add orca https://github.com/OrcaSlicer/OrcaSlicer.git
git fetch orca --tags
```

Find the OrcaSlicer tag VerSlicer was imported from: the tag whose `src/libslic3r` differs least from our first commit (`d50f8df9`):

```bash
for t in $(git tag -l 'v2.*' --sort=-v:refname | head -8); do
  printf '%s %s\n' "$(git diff --shortstat d50f8df9 "$t" -- src/libslic3r | awk '{print $4+$6}')" "$t"
done | sort -n | head -3
```

Then record it as the merge base with a graft-style merge that keeps our tree:

```bash
git checkout -b upstream-base main
git merge -s ours --allow-unrelated-histories <that-tag> -m "Record OrcaSlicer <that-tag> as merge base"
git checkout main && git merge upstream-base
```

From then on, upstream releases merge normally.

## Each sync (every OrcaSlicer release, or quarterly)

```bash
git fetch orca --tags
git checkout -b sync/orca-<version> main
git merge <new-orca-tag>
```

1. Conflicts in VerSlicer folders: keep ours (`git checkout --ours <file>`).
2. Conflicts in hook files: take theirs, then re-add our `// VerSlicer:` calls. `./scripts/verslicer_hooks.sh` lists the files to check.
3. Re-apply branding if upstream touched it (`version.inc`, platform files).
4. Translations: run the Korean catalog through `msgmerge`, then `msgfmt --check-format`, and fill new empty entries.
5. Build and test:
   ```bash
   ./build_dev_macos.sh
   ./scripts/run_orca_tools_test.sh
   ```
6. Go through `docs/QA_CHECKLIST.md`, then open a pull request into `main`.
