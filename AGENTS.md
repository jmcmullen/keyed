# Agent Guidelines

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

## Conventions
- No backwards compatibility - refactor to single clean implementation
- Styling: react-native-unistyles with theme callbacks
