# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

MCP server for Unreal Engine 5 (5.0-5.7). Dual-process architecture: a TypeScript MCP server (`src/`) communicates over WebSocket with a native C++ Automation Bridge plugin (`plugins/McpAutomationBridge/`). 36 consolidated tools use action-based dispatch. Published on npm as `unreal-engine-mcp-server`.

## Commands

```bash
npm run build          # Clean + compile TypeScript (tsc)
npm run build:core     # TypeScript only (no clean)
npm run build:watch    # Watch mode
npm run lint           # ESLint (TS files only, flat config)
npm run lint:fix       # ESLint with auto-fix
npm run type-check     # tsc --noEmit
npm run test:unit      # Vitest unit tests (no Unreal needed)
npm run test:unit:watch # Vitest watch mode
npm run test:smoke     # Smoke test (mock mode, no Unreal needed)
npm test               # Integration tests (requires running Unreal Editor with plugin)
npm run dev            # Run via ts-node-esm
npm start              # Run built output (node dist/cli.js)
```

Run a single unit test: `npx vitest run path/to/file.test.ts`

## Architecture

### Dual-Process Flow

1. **MCP Server (TypeScript)**: Receives tool calls via MCP SDK (stdio JSON-RPC) -> validates with JSON schemas -> dispatches to handler
2. **WebSocket Bridge**: Handler calls `executeAutomationRequest()` -> sends JSON payload to C++ plugin over WebSocket
3. **UE Plugin (C++)**: `UMcpAutomationBridgeSubsystem` receives request -> dispatches to registered handler on Game Thread -> returns JSON result

### Key Files

| Purpose | File |
|---------|------|
| Tool schemas & action enums | `src/tools/consolidated-tool-definitions.ts` |
| Tool routing/registration | `src/tools/consolidated-tool-handlers.ts` |
| Domain handler implementations | `src/tools/handlers/*-handlers.ts` (40 files) |
| Common handler utilities | `src/tools/handlers/common-handlers.ts` |
| WebSocket bridge client | `src/automation/bridge.ts` |
| Bridge handshake protocol | `src/automation/handshake.ts` |
| Path normalization | `src/utils/normalize.ts` |
| Console command safety filter | `src/utils/command-validator.ts` |
| Response validation (AJV) | `src/utils/response-validator.ts` |
| Server entry & stdout routing | `src/index.ts` |
| C++ handler registration | `plugins/.../Private/McpAutomationBridgeSubsystem.cpp` |
| C++ native handlers | `plugins/.../Private/McpAutomationBridge_*Handlers.cpp` |
| C++ safety helpers (5.7 compat) | `plugins/.../Private/McpAutomationBridgeHelpers.h` |

### Adding a New Action (End-to-End)

1. **TS schema**: Add action to the tool's enum + input/output schemas in `src/tools/consolidated-tool-definitions.ts`
2. **TS routing**: Register handler in `src/tools/consolidated-tool-handlers.ts` or the appropriate `src/tools/handlers/*-handlers.ts`
3. **C++ handler**: Implement in `plugins/.../Private/McpAutomationBridge_*Handlers.cpp`, register in `UMcpAutomationBridgeSubsystem::InitializeHandlers()`
4. **Tests**: Add integration test case in `tests/`

## Conventions

### TypeScript

- **ESM throughout**: `"type": "module"`, `"module": "NodeNext"`. Use `.js` extensions in imports.
- **Strict mode**: All strict checks enabled. Avoid `as any` in runtime code; use `unknown` or proper interfaces.
- **No stdout pollution**: MCP uses stdio for JSON-RPC. Runtime logs must go through `Logger` (routed to stderr). Never use `console.log` in runtime code.
- **Tool registration**: Always use `toolRegistry.register()`. Never call handlers directly or use raw WebSocket calls — use `executeAutomationRequest()`.
- **Path handling**: Use `/Game/` prefix for asset paths. Normalization in `src/utils/normalize.ts` converts `/Content` to `/Game`.
- **Unused vars**: Prefix with `_` (ESLint pattern: `argsIgnorePattern: '^_'`).

### C++ Plugin

- Follow Unreal Engine coding standards (`UPROPERTY`/`UFUNCTION` macros).
- **UE 5.7 safety**: Do NOT use `UPackage::SavePackage()` directly (access violations). Use `McpSafeAssetSave` from helpers.
- **SCS components**: Create via `SCS->CreateNode()` and `AddNode()`.
- **`ANY_PACKAGE`**: Deprecated in 5.7. Use `nullptr` for path lookups.

### Testing

- **Unit tests** (Vitest): Colocated as `*.test.ts` alongside source, plus `tests/unit/`. Run with `npm run test:unit`.
- **Integration tests**: Custom MCP runner in `tests/test-runner.mjs`. Requires Unreal Editor running with plugin.
- **Mock mode**: `MOCK_UNREAL_CONNECTION=true` env var forces bridge connection to succeed for offline CI/testing.

### PR Process

- Conventional Commits for PR titles: `feat:`, `fix:`, `docs:`, `refactor:`, etc.
- Run `npm run lint` and `npm run test:smoke` before submitting.
- Update `CHANGELOG.md` under `[Unreleased]`.

## Connection

- UE plugin listens on `127.0.0.1` ports `8090,8091` by default.
- Override with `MCP_AUTOMATION_HOST`, `MCP_AUTOMATION_PORT`, or `MCP_AUTOMATION_WS_PORTS` env vars.
- LAN access: set `MCP_AUTOMATION_ALLOW_NON_LOOPBACK=true` and `MCP_AUTOMATION_HOST=0.0.0.0` (both TS and plugin sides).

## Version

Version is tracked in: `package.json`, `server.json`, `src/index.ts`. Bumped atomically via `.github/workflows/bump-version.yml`.
