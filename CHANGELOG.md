# Changelog

## 3.2.0

### Assistant
- ChatGPT is now the only assistant backend; the bundled Ollama runtime and model are gone, so the app is several GB smaller.
- New **printer_diagnostics** tool: ask "my print failed" and the assistant reads the printer's error code and HMS alerts, names the likely cause area, and suggests settings to check.
- A one-time notice explains what is sent to OpenAI before the assistant is used.
- Every dangerous action (settings, preset switch, send, export, save, delete) goes through the review dialog. If no window can show it, nothing is applied.

### Security
- ChatGPT tokens are stored in the macOS Keychain, not a file.
- The local MCP server for external agents is off by default. When enabled, it uses a cryptographically random token in an owner-only file.

### Fixes
- Korean particles such as "에서" are no longer left in MakerWorld search queries.
- Update checks now look at VerSlicer releases instead of OrcaSlicer's update server.
- No more attempt to start `ollama serve` after loading a model.

### Project
- Licensed under AGPL-3.0 throughout; see NOTICE.
- Headless tests run on GitHub Actions. Tagged releases build the DMG automatically.
