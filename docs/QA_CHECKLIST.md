# Manual QA checklist

Run on a **Release** build installed from the DMG, on a Mac that has not run VerSlicer before (or after deleting `~/Library/Application Support/verslicer`). Tick each line; file an issue for any failure.

## Install and launch
- [ ] DMG opens; dragging to Applications works; first launch shows no "damaged" or "unidentified developer" warning (signed builds only).
- [ ] About shows version 3.2.x.
- [ ] No Keychain password prompt on launch, including right after installing an update over a connected 3.2.0.

## Privacy notice and ChatGPT
- [ ] Press **Connect ChatGPT** → the data notice appears first; **Cancel** does nothing further.
- [ ] Agree → browser opens → sign in and allow → panel shows your email and **Sign out**; mode, model and **New chat** sit on one row.
- [ ] Quit and relaunch: still connected, the data notice does not appear again, no Keychain prompt.
- [ ] **Sign out** → confirm → button returns to **Connect ChatGPT**.
- [ ] Disconnect Wi-Fi and send a message → "Can't reach ChatGPT…" in your language.

- [ ] Expired sign-in: if the panel ever shows a sign-in error, it offers **Connect ChatGPT** again (no raw `invalid_grant`).

## Printing (Bambu printer)
- [ ] With a Bambu printer selected, the print button reads **Print with Bambu Connect**.
- [ ] Bambu Connect not installed → pressing it explains why and **Download Bambu Connect** opens the Bambu wiki download section (not a 404).
- [ ] Bambu Connect installed → slice, press it → Bambu Connect opens with the sliced file ready to print.
- [ ] Print button menu → **Print plate** → relaunch: the button still reads Print plate.
- [ ] Device tab with a cloud-connected Bambu printer: the Bambu Connect note shows; **Dismiss** hides it for good; **Open Bambu Connect** opens the app (or the download page).
- [ ] Smart Print panel shows readiness, workflow and insights; the link at the bottom opens Preferences on the Smart Print tab.

## Assistant actions
- [ ] Load a model. "Rotate it 45 degrees" → rotates without a review dialog.
- [ ] "Make this stronger" → review dialog lists the setting changes; **Cancel** leaves settings unchanged; **Apply** changes them.
- [ ] "Undo the last AI change" → settings return.
- [ ] "Slice it" → slices; nothing is sent to the printer.
- [ ] "Send it to my printer" → the AI says to press the visible Print button; no send dialog opens on its own.
- [ ] File → Import has no "Create Model"; the 3D view's options menu (next to 3D Navigator) has no "Create Model".
- [ ] Empty plate: "용 피규어 출력해줘" → MakerWorld pick dialog → import → slice → "press the Print button" message (no automatic send).
- [ ] With a Bambu printer connected and an active HMS alert: "My print failed" → the reply names the error code and suggests settings.
- [ ] Without a printer: "My print failed" → asks you to connect one or describe the failure.
- [ ] Question mode: "Make this stronger" → explanation only, no dialog, no change.

## Printer control (needs a connected Bambu printer)
- [ ] "Set the nozzle to 215" → "AI printer control" dialog; **Don't allow** → nothing changes and the AI says so.
- [ ] Ask again → **Allow** → chat says "Printer set: nozzle temperature 215 °C". In LAN/Developer Mode the Device tab shows the new target; over the cloud the chat adds that the printer may ignore it.
- [ ] "Set the bed to 200" → refused with the printer's limit; nothing sent.
- [ ] "Set the chamber to 50" on a printer not in heating mode → refused, tells you to use the Device tab.
- [ ] Not printing: "Switch to sport speed" → refused (only while printing).
- [ ] Smart Print → **Allow AI printer control** is checked; uncheck it → next request asks again.

## Voice
- [ ] Mic button asks for Microphone and Speech Recognition permission once; a Korean phrase is transcribed.

## Crash reporting
- [ ] `kill -SEGV $(pgrep -f VerSlicer.app/Contents/MacOS)` → relaunch → "VerSlicer quit unexpectedly…" → **Open issue** opens a prefilled GitHub issue with no `/Users/<name>` paths.

## Updates
- [ ] With a newer published GitHub release, Help → Check for Updates offers it; with none, it says you're up to date.

## Local MCP server (optional)
- [ ] Off by default: nothing listening on 127.0.0.1:8765.
- [ ] With `"mcp_server": "true"`: `~/Library/Application Support/verslicer/mcp.json` is `-rw-------`.

## Smart Print tools via chat (needs ChatGPT)
- [ ] Load a model. "아이가 가지고 놀 장난감 뽑을래" → review dialog headed "Expected effect: …", reason mentions the toy purpose; **Apply** changes walls/infill.
- [ ] "이거 잘 뽑힐까?" → overhangs turn red on the 3D view, the worst model is selected, the reply lists problems with a fix for each.
- [ ] "내 프린터에 대해 뭘 배웠어?" → success rate and adjustments, each adjustment with "because …"; nothing changes.
- [ ] Right-click a setting label in the Process tab → **Explain with AI** → the chat explains that setting.
- [ ] Smart Print menu → **Undo last AI change** → the last applied change is reverted.

## Camera watch (needs a Bambu printer with a camera)
- [ ] "카메라 감시 켜져 있어?" → lists each check as on/off/not supported; nothing changes, no consent dialog.
- [ ] LAN + Developer Mode: "출력 중에 카메라로 감시해줘" → consent dialog → **Allow** → within ~15 s the chat says the printer confirmed. Device tab → camera settings show the checks on.
- [ ] Cloud printer: same request → after ~15 s the chat explains the cloud ignores it (no false "done").

## Learning from print results (needs a print that finishes)
- [ ] Print finishes → notification "Print finished. Tell VerSlicer how it came out…" with **How did it come out?**
- [ ] Click it → choose **It came out well** → "Recorded as a good print".
- [ ] Next print: choose **Corners lifted or warped** → notification names what changes next time (e.g. slower first layer, warmer bed, wider brim).
- [ ] Smart Print → **What VerSlicer learned…** → success rate dropped by one print; "You reported 1 problem(s)…"; adjustments show "because the first layer came loose 1 time(s)".
- [ ] Smart Print quick apply on a new plate → the review lists the learned change with "Learned from this printer: …".
- [ ] In chat: "아까 출력 실이 엄청 늘어졌어" → the AI records it (stringing) and says retraction will go up.

## Live AI accuracy (needs ChatGPT; ~20 minutes)
The golden cases in `src/dev-utils/eval/assistant_cases.json` say which tool each request must use. To measure the real model:

```bash
./scripts/assistant_live_eval.sh start
```

This writes the requests to `build/assistant_eval_requests.txt` and starts the dev build with `VERSLICER_EVAL_LOG` set. With a model on the plate, paste each request into the chat in **Apply** mode, pressing **New chat** between requests and **Cancel** on every review dialog. Then:

```bash
./scripts/assistant_live_eval.sh grade
```

It prints the pass rate per model and each failed case with the reason. Record the result in the release notes; investigate any case that fails twice in a row.
