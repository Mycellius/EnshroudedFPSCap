# Notices

## AI-generated code

EnshroudedBaseCap was built with **Claude Code**, Anthropic's AI coding assistant (model: Claude Opus 5.5).
The AI wrote most of the source code, build scripts, tests and documentation.
The maintainer (Mycellius) set the requirements, chose the design, and tested the result in Enshrouded.
Commits written with the AI carry a `Co-Authored-By: Claude` trailer.

Treat it the way you'd treat code from any contributor you don't know: read it before you trust it.
The source is short; most of it is `src/basecap.cpp`.

## No warranty

The software is provided "as is", without warranty of any kind, and the authors aren't liable for anything that comes of using it. The full terms are in [LICENSE](LICENSE) (MIT).
It hooks a function inside a running game. Use it at your own risk.

## Not affiliated

This is an unofficial fan project. It isn't made, endorsed or supported by Keen Games (Enshrouded), NVIDIA (DLSS, Streamline, Reflex), Nexus Mods, or Biozilla (author of the Enshrouded DLSS Frame Generation mod it plugs into).
All trademarks belong to their owners.
EnshroudedBaseCap contains no code or files from Enshrouded, NVIDIA, or the frame generation mod.

## Third-party software

| Component | Used for | License |
|---|---|---|
| [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4, © 2009–2017 Tsuda Kageyu, including Hacker Disassembler Engine, © 2008–2009 Vyacheslav Patkov | hooking `vkQueuePresentKHR` | BSD 2-Clause |

MinHook is compiled into `EnshroudedBaseCap.dll`. Its license is in [third_party/minhook/LICENSE.txt](third_party/minhook/LICENSE.txt), and in release zips as `EnshroudedBaseCap-docs/MinHook-LICENSE.txt`.
