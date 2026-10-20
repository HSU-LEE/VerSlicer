# Changelog

## 3.2.1

### Printing to Bambu Lab printers
- New **Print with Bambu Connect** in the print button menu, and the default for Bambu printers. It hands the sliced plate to Bambu Connect, Bambu Lab's helper app, which is how printers with Authorization Control firmware accept jobs from slicers other than Bambu Studio — no Developer Mode needed. Choose **Print plate** to keep printing directly over LAN; the choice is remembered.
- Smart Print's one-click Print follows the same choice.
- The Device tab shows a short, dismissible note for cloud-connected Bambu printers, with an **Open Bambu Connect** button (or a link to download it).

### AI assistant
- Find-and-print now ends by telling you to press the Print button on screen. On a Bambu printer that button is **Print with Bambu Connect**, not Print plate.
- An expired ChatGPT sign-in ("invalid_grant") no longer leaves the panel looking connected with a raw error code; it asks you to connect again.
- Mode, model and **New chat** share one row, leaving more room for the conversation.
- On cloud-connected printers, AI printer control says the printer may ignore the command instead of claiming success.
- Request routing and MakerWorld search now rely on ChatGPT's intent and query instead of local keyword lists.
- macOS no longer asks for your login Keychain password after an update. The ChatGPT sign-in is kept in a file only your user can read; a sign-in saved by 3.2.0 moves there once, without a prompt.
- Free-plan ChatGPT accounts get a notice that the assistant needs ChatGPT Plus or Pro.
- A ChatGPT reply could run the same action twice (for example rotating twice); each action now runs once.
- Quitting from the Dock, menu or logout could end in a crash report from the Bambu network plugin (#5); VerSlicer now exits after saving without running the plugin's shutdown code.

### Printers
- Picking a printer that is connected through your Bambu Lab account (not LAN mode) shows how to turn on LAN Only mode and Developer Mode.

### Smart Print
- The docked panel focuses on the plate: readiness, workflow, insights. Account, assistant options, history and printer learning moved to **Preferences → Smart Print**, opened from the link at the bottom.

### Project
- The DMG opens a styled install window (drag VerSlicer to Applications, plus the Terminal fix for a blocked first launch).
- New .pkg installer next to the DMG; its screens show the Terminal fix as copyable text.
- CI: the dependency cache step no longer times out, and runs on main are not cancelled by the next push.

## 3.2.0

### Assistant
- The AI no longer sends jobs to the printer or creates geometry. AI Model Create and the hole/handle tool are removed; find-and-print stops after slicing and asks you to press **Print plate**.
- New, opt-in **printer control**: once you allow it, the AI can set nozzle, bed and chamber target temperature and print speed level on the connected printer, within the printer's limits. Toggle it in **Smart Print → Allow AI printer control**.
- ChatGPT is now the only assistant backend; the bundled Ollama runtime and model are gone, so the app is several GB smaller.
- New **printer_diagnostics** tool: ask "my print failed" and the assistant reads the printer's error code and HMS alerts, names the likely cause area, and suggests settings to check.
- A one-time notice explains what is sent to OpenAI before the assistant is used, and is not shown again once accepted.
- Every dangerous action (settings, preset switch, export, save, delete) goes through the review dialog. If no window can show it, nothing is applied.

### Security
- ChatGPT tokens are stored in the macOS Keychain, not a file.
- The local MCP server for external agents is off by default. When enabled, it uses a cryptographically random token in an owner-only file.

### Fixes
- No more crash on every Quit (Cmd+Q / Dock): the Bambu network plugin is shut down before macOS exits the app.
- Fixed crashes when a background task finished during quit, and when ChatGPT returned unexpected JSON.
- Korean particles such as "에서" are no longer left in MakerWorld search queries.
- Update checks now look at VerSlicer releases instead of OrcaSlicer's update server.
- No more attempt to start `ollama serve` after loading a model.

### Project
- Licensed under AGPL-3.0 throughout; see NOTICE.
- Headless tests run on GitHub Actions. Tagged releases build the DMG automatically.
