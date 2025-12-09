# Keyed - UI Specification

## App Identity

**Name:** Keyed
**Subtitle:** DJ BPM & Key Finder
**Short description:** Detect the key and BPM of any song playing around you, completely offline. Built for DJs mixing vinyl and digital.

---

## Design Principles

- Dark mode only
- Large, high-contrast typography (readable at a glance in dim lighting)
- Minimal navigation (two screens total)
- Subtle, non-distracting animations

---

## Screens

### 1. Main Screen (Detection)

#### Layout (top to bottom)

1. **Header**
   - App name "Keyed" (subtle, top left)
   - History icon (top right) → navigates to History screen

2. **BPM Display**
   - Large primary number (e.g. "122")
   - "BPM" label
   - Confidence percentage with colour coding:
     - Green: high confidence (>80%)
     - Amber: medium confidence (50-80%)
     - Red: low confidence (<50%)
   - Tap tempo button (small, nearby) → expands into tap zone when activated

3. **Key Display**
   - Standard notation large (e.g. "Am")
   - Camelot code below, smaller (e.g. "8A")
   - Confidence percentage with colour coding
   - Compatible keys shown subtly below (e.g. "Mix with: 7A · 9A · 8B")

4. **Waveform Visualiser**
   - Subtle waveform animation that reacts to microphone input
   - Only visible while listening

5. **Action Area**
   - Idle state: "Tap to detect" button/prompt
   - Listening state: option to tap again to stop manually

#### States

| State | BPM Display | Key Display | Waveform | Action |
|-------|-------------|-------------|----------|--------|
| Idle | Hidden or "---" | Hidden or "--" | Hidden | "Tap to detect" |
| Listening | Updating live | Updating live | Animating | "Tap to stop" (optional) |
| Result | Final value + confidence | Final value + confidence | Fades out | "Tap to detect" (reset) |

#### Behaviour

- Tap to start listening
- Auto-stop after ~5 seconds of silence
- Optional manual stop by tapping again
- On stop: save entry to history automatically

---

### 2. History Screen

#### Layout

1. **Header**
   - "History" title
   - Back button/icon → returns to Main screen
   - Clear all button (top right, or in overflow menu)

2. **List**
   - Scrollable list of detection entries, most recent first
   - Empty state: "No history yet"

#### List Item

Each row displays:

| Field | Format | Example |
|-------|--------|---------|
| BPM | Number | 122 |
| Key | Standard notation | Am |
| Camelot | Code | 8A |
| Timestamp | Relative (<1hr) or absolute with date | "2 min ago" or "11:42 PM · 5 Dec" |
| Duration | Seconds | 12s |

#### Interactions

- Swipe left to delete individual entry
- Clear all button to remove all history (with confirmation prompt)

---

## Tap Tempo

- Small button positioned near the BPM display
- On tap: expands into a larger tap zone
- User taps repeatedly to set tempo manually
- Calculates BPM from tap intervals
- Shows live BPM as user taps, replacing the main BPM display
- Timeout after ~3 seconds of no taps → collapses back to button, final BPM remains on screen
- Does not save to history (manual tempo only)

---

## Data Persistence

- History persists across app restarts using Drizzle ORM + Expo SQLite
- Detection entries saved automatically on stop

---

## Colour Palette

| Element | Colour |
|---------|--------|
| Background | Near black (#0A0A0A) |
| Primary text | White (#FFFFFF) |
| Secondary text | Grey (#888888) |
| Accent | Brand colour TBD |
| Confidence high | Green (#22C55E) |
| Confidence medium | Amber (#F59E0B) |
| Confidence low | Red (#EF4444) |

---

## Typography

| Element | Size | Weight |
|---------|------|--------|
| BPM number | 72-96pt | Bold |
| Key (standard) | 48-64pt | Bold |
| Camelot code | 24-32pt | Medium |
| Confidence % | 16pt | Regular |
| Compatible keys | 14pt | Regular |
| History list item | 16pt | Regular |

---

## Iconography

- History: Clock or list icon
- Tap tempo: Metronome or hand/tap icon
- Microphone: Waveform or mic icon for listening state
- Delete: Trash icon

---

## Future Considerations (out of scope for v1)

- Light mode toggle
- Manual track naming in history
- Export history as CSV/playlist
- Apple Watch / Wear OS companion
- Widget for home screen
