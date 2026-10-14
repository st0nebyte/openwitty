# Reverse-engineering toolchain (root-free)

Nothing here is a runtime dependency of the application. All of it lives in
`./.tools` (git-ignored). `tools/setup-rootless.sh` reproduces it (pinned
versions; checksums verified for Node, JDK, Ghidra, innoextract source).
**Caveat:** the script mirrors the manual steps that were run in this
session; it has not been executed end-to-end from scratch.

| Tool | Version | Why this form |
|---|---|---|
| Node.js | 24.21.0 (nodejs.org tarball) | system Node is 25.9, outside REA's supported range (22.19+/24.11+/26+) |
| JDK | Temurin 21.0.12.1 | Ghidra 12.1 needs JDK ≥ 21 (system has 11) |
| Ghidra | 12.1.4 | REA's Ghidra provider accepts 12.1.x |
| innoextract | upstream master `6e9e34e`, built locally | release 1.9 only reads Inno ≤ 6.0.5; the installer is 6.3.0 |
| REA | rea-agents 6.4.0 (npm, `--ignore-scripts`) | see below |

Activate: `source .tools/rea-env.sh`, then `rea doctor --json` (read-only).

## REA evaluation
* Official sources: github.com/morluto/rea, npm `rea-agents` (MIT).
* Package has no install/postinstall scripts; installed locally, nothing global.
* `rea doctor` here: Ghidra 12.1.4, JDK 21, linux-x64 native decompiler,
  procps `ps` all **healthy**. Hopper/IDA absent (not needed).
* **Works** (CLI): `rea analyze` on the stripped Windows PE → 24,833
  functions, 13,900 strings, via Ghidra. `rea search` returns RTTI strings.
* **Cost:** each CLI call re-imports the binary (~2 min for this 11 MB PE).
  The MCP server keeps a session open, which is its main advantage for
  iterative function/xref work.
* **Not done: MCP registration.** `rea setup` writes `mcpServers.rea` into
  `~/.claude.json` and installs a skill under `~/.claude/skills` — outside
  this project — so it needs the owner's approval. A backup-and-additive
  procedure is described in REA's `docs/installation.md`; `rea setup --dry-run`
  previews it. The Claude Code session must be restarted afterwards.
* Limits: REA is an *evidence* tool; decompiler output is not ground truth.
  Our protocol facts are cross-checked against disassembly (`tools/re/*`).

## What each tool contributed
* macOS binary has full symbols → our capstone/LIEF scripts (`tools/re`) were
  enough for the protocol (transport, message table, field offsets).
* Windows binary is stripped (RTTI only) → REA/Ghidra needed for function
  recovery there; used so far only to confirm scope and the Thesycon API
  names (below).

## Windows installer extraction
`.tools/ie-build/innoextract -d research/win_extracted controlcenterinstallerwin_5_2_2_1.exe`
yields `app/LEWITT CONTROL CENTER.exe` (PE32+, 11.5 MB), presets, tooltips, the
Thesycon USB audio driver installer (v5.68.0) and VC redist. Same codebase as
macOS (same `LWT::Connect2::APIv1`, `buildConnect2Tree`, `doAsyncFWDownload`
RTTI names). Windows resolves `TUSBAUDIO_AudioControlRequestGet/Set` and
`TUSBAUDIO_ClassVendorRequestIn/Out` from Thesycon's user-mode API: on Windows
the control traffic goes through Thesycon's own kernel driver.
