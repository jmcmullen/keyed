# Keyed

**DJ BPM & Key Finder**

Detect the key and BPM of any song playing around you, completely offline. Built for DJs mixing vinyl and digital.

## Features

- **Real-time detection** — Instantly identifies BPM and musical key via your device's microphone
- **Confidence scoring** — See how accurate each reading is
- **Fully offline** — No internet required, no data leaves your device
- **Cross-platform** — Available on iOS and Android

## Privacy

Keyed processes all audio locally on your device. No audio data, song information, or usage analytics are ever collected or transmitted. Your listening stays private.

## Tech Stack

- [Expo](https://expo.dev) / React Native
- TypeScript
- [react-native-unistyles](https://github.com/jpudysz/react-native-unistyles) for styling
- [TanStack Query](https://tanstack.com/query) for state management
- Turborepo monorepo structure

## Development

### Prerequisites

- [Bun](https://bun.sh) v1.2.16+
- iOS Simulator / Android Emulator or physical device
- Xcode (for iOS) / Android Studio (for Android)

### Setup

```bash
# Clone the repository
git clone https://github.com/jmcmullen/keyed.git
cd keyed

# Install dependencies
bun install

# Start the development server
bun run dev:native
```

### Scripts

| Command | Description |
|---------|-------------|
| `bun run dev:native` | Start Expo development server |
| `bun run build` | Build all packages |
| `bun run check` | Run linting and formatting |
| `bun run check-types` | TypeScript type checking |

## Project Structure

```
keyed/
├── apps/
│   └── native/       # React Native mobile app
├── packages/
│   └── config/       # Shared TypeScript configuration
└── ...
```

## Contributing

Contributions are welcome. Please open an issue first to discuss what you'd like to change.

1. Fork the repository
2. Create your branch (`git checkout -b feature/your-feature`)
3. Commit your changes
4. Push to the branch
5. Open a pull request

## License

[MIT](LICENSE)
