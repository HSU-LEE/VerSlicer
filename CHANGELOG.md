# Changelog

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
