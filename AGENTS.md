# Agent Guidelines

## Operating Rules
- ALWAYS USE PARALLEL TOOLS WHEN APPLICABLE.
- The default branch in this repo is `master`.
- Local `main` ref may not exist; use `master` or `origin/master` for diffs.
- Prefer automation: execute requested actions without confirmation unless blocked by missing info or safety/irreversibility.

## Commands
- `bun run check` - lint and format (biome)
- `bun run check-types` - typecheck all packages
- `bun run dev` - run all apps
- `bun run dev:native` - run native app only

## Code Style
- **Indent:** tabs
- **Quotes:** double quotes
- **Files:** kebab-case (`header-button.tsx`)
- **Components:** PascalCase exports (`export const HeaderButton`)
- **Imports:** external first, then internal with `@/` alias; always at top of file (no dynamic imports)
- **Exports:** named exports for components, default exports for route pages
- **Types:** inline prop types `({ children }: { children: React.ReactNode })`; strict mode enabled

## Biome Rules
- `useSortedClasses` for Tailwind (clsx, cva, cn)
- `noParameterAssign`, `useAsConstAssertion`, `useSelfClosingElements` enforced

## General Principles
- Keep things in one function unless composable or reusable.
- Avoid `try/catch` where possible.
- Avoid using the `any` type.
- Prefer single-word names for variables and functions where possible.
- Use Bun APIs when possible (for example, `Bun.file()`).
- Rely on type inference when possible; avoid explicit type annotations or interfaces unless needed for exports or clarity.
- Prefer functional array methods (`flatMap`, `filter`, `map`) over `for` loops; use type guards on `filter` to maintain downstream type inference.

## Naming
- Prefer single-word names for variables and functions. Only use multiple words when necessary.

## Naming Enforcement (Mandatory)
- Use single-word names by default for new locals, params, and helper functions.
- Multi-word names are allowed only when a single word would be unclear or ambiguous.
- Do not introduce new camelCase compounds when a short single-word alternative is clear.
- Before finishing edits, review touched lines and shorten newly introduced identifiers where possible.
- Good short names to prefer: `pid`, `cfg`, `err`, `opts`, `dir`, `root`, `child`, `state`, `timeout`.
- Examples to avoid unless truly required: `inputPID`, `existingClient`, `connectTimeout`, `workerPath`.

```ts
// Good
const foo = 1
function journal(dir: string) {}

// Bad
const fooBar = 1
function prepareJournal(dir: string) {}
```

- Reduce total variable count by inlining when a value is only used once.

```ts
// Good
const journal = await Bun.file(path.join(dir, "journal.json")).json()

// Bad
const journalPath = path.join(dir, "journal.json")
const journal = await Bun.file(journalPath).json()
```

## Destructuring
- Avoid unnecessary destructuring. Use dot notation to preserve context.

```ts
// Good
obj.a
obj.b

// Bad
const { a, b } = obj
```

## Variables
- Prefer `const` over `let`.
- Use ternaries or early returns instead of reassignment.

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
- Test actual implementation; do not duplicate logic into tests.

## Conventions
- No backwards compatibility; refactor to a single clean implementation.
- Styling: `react-native-unistyles` with theme callbacks.
