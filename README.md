<p align="center">
  <img src="resources/images/Verslicer.svg" alt="VerSlicer" width="140" />
</p>

# VerSlicer

### AI-Powered 3D Printing Slicer

> [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) fork with [Ollama](https://ollama.com/) on your Mac — chat and voice

<p align="center">
  <img src="docs/images/main-ui.png" alt="VerSlicer with Ollama chat open" width="900" />
</p>

## Demo

<p align="center">
  <video src="docs/test.mp4" width="900" controls playsinline poster="docs/images/main-ui.png">
    <a href="docs/test.mp4"><img src="docs/images/main-ui.png" alt="Play demo" width="900" /></a>
  </video>
</p>

<p align="center">
  <a href="docs/test.mp4"><strong>Watch demo (MP4)</strong></a>
</p>

**VerSlicer** starts from OrcaSlicer and adds an assistant that can change the slicer for you — presets, parts on the bed, slice, and the usual menus — from a chat window on the 3D view.

Type what you want, or use the mic on macOS. Ollama runs locally; the app turns the reply into real steps. No separate “advisor” window that only talks: it uses the same plater you already have.

Bambu network plugin, cloud, Device tab, and Smart Print are still there. Builds are **macOS-only** right now.

## Examples

Works in English or Korean for many requests:

| You might say | |
| --- | --- |
| *Rotate this to use less support* | moves / rotates the selection |
| *Lay it flat* / *Flip it* | flip & rotate (flip defaults to 180° on X) |
| *Layer height 0.12* | print preset |
| *Make it stronger* | walls, infill, etc. |
| *Supports on* | `enable_support` |
| *Arrange the plate* | auto-arrange |
| *Slice* | slice current plate |
| *Preview* | switch tab |

Voice on macOS: hit the mic on the toolbar, speak, send.

Implementation lives in [`src/slic3r/GUI/OllamaAssistant/`](src/slic3r/GUI/OllamaAssistant/) — chat UI, HTTP to Ollama, JSON `actions` (`set_config`, `slice`, `rotate`, `arrange`, …).

## Setup

```bash
ollama pull llama3.2
```

Install [Ollama](https://ollama.com/), leave it running, open VerSlicer, then **Ollama chat** on the 3D toolbar. Change host or model in the panel if you use something other than the default.

## Build

macOS 11.3+, Xcode or CLT, CMake 3.13+.

```bash
./build_release_macos.sh       # deps + app
./build_release_macos.sh -x    # Ninja, nicer for dev
./build_release_macos.sh -s    # app only
```

Output under `build/<arch>/`. DMG: `./build_release_macos.sh -s -x -M`.

## Where this is going

Right now the assistant is good at everyday plate and preset work — tune, orient, slice — without treating the slicer like a settings encyclopedia.

Later I want to go further (e.g. helping when prints fail, from logs).

## License & copyright

VerSlicer is developed based on OrcaSlicer.

OrcaSlicer and related upstream code remain under their original open-source licenses; copyright belongs to the upstream authors.

Additional features, UI improvements, and AI-related functionality in VerSlicer are copyright **Lee Hee Seung**. See [LICENSE](LICENSE).

## Links

- [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer)
- [Ollama](https://ollama.com/) · [API docs](https://github.com/ollama/ollama/blob/main/docs/api.md)
