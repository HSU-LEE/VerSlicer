<p align="center">
  <img src="resources/images/Verslicer.svg" alt="VerSlicer" width="140" />
</p>

# VerSlicer

Fork of [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) with Bambu Lab workflow kept intact, plus an Ollama-backed chat panel in the 3D view. You can ask it to change print settings, move/rotate models, slice, and a few other things. Voice input works on macOS.

macOS only for now (build script is `build_release_macos.sh`).

## What’s in here

- Same slicing stack as OrcaSlicer (presets, multi-plate, preview, etc.)
- Bambu network plugin, cloud, Device tab, Smart Print UI
- Floating Ollama chat + optional voice button on the plater toolbar
- Model replies with JSON; the app runs the `actions` list ([`OllamaAssistant/`](src/slic3r/GUI/OllamaAssistant/))

## Ollama

Install [Ollama](https://ollama.com/) and pull a model. The UI defaults to `llama3.2` at `http://127.0.0.1:11434`; change host/model in the chat panel if you need to.

```bash
ollama pull llama3.2
```

Open **Ollama chat** from the 3D toolbar. On macOS, **Voice** sends transcribed text into the chat. The assistant gets current preset keys and plate context, then returns one JSON object. Supported `actions[].type` values include:

| `type` | |
| --- | --- |
| `set_config` | print / filament / printer options |
| `ui_select_tab` | e.g. prepare, preview, monitor |
| `slice` | plate or all |
| `translate`, `rotate`, `scale` | selection on the bed |
| `delete_selection`, `clone_selection`, `arrange` | |
| `add_model` | absolute path to STL, 3MF, etc. |
| `menu_item` | run a menu entry by name |

## Build

Needs macOS 11.3+, Xcode or CLT, CMake 3.13+. Use Ninja if you pass `-x`.

```bash
./build_release_macos.sh       # deps + app
./build_release_macos.sh -x    # Ninja multi-config (handy for dev)
./build_release_macos.sh -s    # app only, after deps are built
./build_release_macos.sh -h    # other flags
```

Output under `build/<arch>/`. DMG: `./build_release_macos.sh -s -x -M`.

## Layout

```
src/slic3r/GUI/OllamaAssistant/   chat UI, HTTP client, action runner
src/libslic3r/                    slicing core
build_release_macos.sh
resources/images/Verslicer.svg
```

## License & copyright

VerSlicer is developed based on OrcaSlicer.

OrcaSlicer and related upstream code stay under their original open-source licenses; copyright remains with the upstream authors.

Additional features, UI changes, and AI-related code in VerSlicer are copyright **Lee Hee Seung**. See [LICENSE](LICENSE) for the MIT terms on those additions.

## Links

- [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer)
- [Ollama](https://ollama.com/) · [API docs](https://github.com/ollama/ollama/blob/main/docs/api.md)
