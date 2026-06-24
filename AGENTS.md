# Agent Guidelines

## Operating Rules
- Follow these rules strictly unless the user gives a direct, task-specific override.
- ALWAYS USE PARALLEL TOOLS WHEN APPLICABLE.
- The default branch in this repo is `master`.
- Local `main` ref may not exist; use `master` or `origin/master` for diffs.
- Prefer automation: execute requested actions without confirmation unless blocked by missing info or safety/irreversibility.
- Do not revert or clean up user changes unless explicitly requested.

## Repository Layout
- `apps/native` - Expo Router app, React Native UI, hooks, local app tests.
- `packages/db` - Drizzle schema, SQLite client, migrations, React provider/hooks.
- `packages/engine` - Expo native module, TypeScript bridge, C++ audio engine, iOS/Android bindings, native tests.
- `packages/config` - shared TypeScript config.

## Commands
- `bun run check` - lint and format with Biome.
- `bun run check-types` - typecheck all packages.
- `bun run dev` - run all apps.
- `bun run dev:native` - run native app only.
- `cd apps/native && bun test tests` - run native app tests.
- `bun run test:native` - build and run C++ engine tests.

## Verification
- When code changes, run `bun run check` and `bun run check-types` before finishing.
- For changes under `apps/native`, run `cd apps/native && bun test tests`.
- For changes under `packages/engine`, run `bun run test:native`.
- If a required check cannot run, report the command, the failure reason, and the risk.
- Because `bun run check` writes formatting changes, inspect the dirty worktree first; if unrelated dirty files make the broad command unsafe, run the narrowest safe Biome command and report the skipped full check.

## Code Style
- **Indent:** tabs.
- **Quotes:** double quotes.
- **Files:** kebab-case (`header-button.tsx`).
- **Components:** PascalCase exports (`export const HeaderButton`).
- **Imports:** external first, then internal with `@/` alias; always at the top of the file; no dynamic imports.
- **Exports:** named exports for components, default exports for route pages.
- **Types:** inline prop types for local components (`({ children }: { children: React.ReactNode })`); exported/shared contracts may use named types or interfaces when clearer.
- Rely on type inference when possible; avoid explicit annotations unless needed for exports, inference boundaries, or clarity.

## Biome
- Biome is the source of truth for formatting, import organization, and lint rules.
- Respect enforced rules including `useSortedClasses`, `noParameterAssign`, `useAsConstAssertion`, `useSelfClosingElements`, `useDefaultParameterLast`, `useEnumInitializers`, `useSingleVarDeclarator`, `noUnusedTemplateLiteral`, `useNumberNamespace`, `noInferrableTypes`, and `noUselessElse`.

## General Principles
- Keep things in one function unless a helper is clearly reusable or reduces meaningful complexity.
- Avoid `try/catch` unless the code can handle or improve the error.
- Avoid `any`.
- Use Bun APIs when practical, for example `Bun.file()`.
- Prefer functional array methods (`flatMap`, `filter`, `map`) over `for` loops when they stay readable; use type guards on `filter` to preserve downstream inference.
- Prefer early returns over nested conditionals.

## Naming
- Prefer short, single-word names for new locals, params, and helper functions.
- Multi-word names are allowed when they are clearer, especially for domain concepts, exported APIs, native bridge types, test names, and values whose role would otherwise be ambiguous.
- Avoid new camelCase compounds when a short single-word alternative is equally clear.
- Before finishing edits, review touched lines and shorten newly introduced identifiers where clarity is preserved.
- Good short names to prefer: `pid`, `cfg`, `err`, `opts`, `dir`, `root`, `child`, `state`, `timeout`.
- Examples to avoid unless they add clarity: `inputPID`, `existingClient`, `connectTimeout`, `workerPath`.

```ts
// Good
const foo = 1
function journal(dir: string) {}

// Bad when the longer name adds no clarity
const fooBar = 1
function prepareJournal(dir: string) {}
```

- Reduce total variable count by inlining values that are only used once and remain readable.

```ts
// Good
const journal = await Bun.file(path.join(dir, "journal.json")).json()

// Bad
const journalPath = path.join(dir, "journal.json")
const journal = await Bun.file(journalPath).json()
```

## Destructuring
- Avoid unnecessary destructuring that hides context.
- Use dot notation when it makes ownership or source clearer.
- Destructuring is fine for common React patterns, tuple hooks, test setup, and values used repeatedly.

## Variables
- Prefer `const` over `let`.
- Use ternaries or early returns instead of reassignment when readable.

```ts
// Good
const foo = condition ? 1 : 2

// Bad
let foo
if (condition) foo = 1
else foo = 2
```

## Control Flow
- Avoid `else` statements. Prefer early returns.

```ts
function foo() {
	if (condition) return 1
	return 2
}
```

## Testing
- Avoid mocks as much as possible.
- Test actual implementation; do not duplicate implementation logic into tests.
- Add or update focused tests for changed behavior.

## Native Projects
- `apps/native/android` and `apps/native/ios` are generated native projects.
- Do not edit them unless the user explicitly requests native project changes.
- Prefer Expo config, module code, or prebuild inputs over generated native edits.

## Conventions
- No backwards compatibility unless explicitly requested; refactor to a single clean implementation.
- Styling: `react-native-unistyles` with theme callbacks.
