## Overview

Players can type messages that appear both in the chat log at the bottom-left of the screen and as overhead text floating above their character in the game world.

## Chat Log

- Persistent message feed fixed to the bottom-left of the screen (560px wide × 210px tall)
- All messages are retained indefinitely — no limit, no expiry, no fade
- Scrollable — the player can scroll up to read older messages
- Content is bottom-aligned: when few messages exist they appear at the bottom; as the log fills, overflow triggers scrolling
- A white divider separates the log from the input line below it
- Two message types:
  - **System messages** (yellow) — game events, examine text, error messages (e.g. "Your inventory is full.")
  - **Player chat** (white) — messages typed and sent by a player, prefixed with their name

## Typing

- The player's name and current typed text are always visible at the bottom of the chat log as a live input line (e.g. `Player: hello_`)
- Any key press that produces a single printable character is added to the input buffer
- Backspace removes the last character
- Enter sends the message (if the buffer is non-empty) and clears the input
- Arrow keys are ignored by the chat input (reserved for camera controls)
- No dedicated input field or focus required — the chat captures all keyboard input passively
- The input area dynamically expands to wrap long messages; the log area shrinks to compensate, keeping the total panel height fixed

## Overhead Chat

- When a player sends a message, it appears as floating yellow text above their character
- The text follows the player's interpolated world position (smooth, no jitter)
- Displayed for 50 ticks (~10 seconds), then fades out over the final 10 ticks
- Only the most recent message is shown overhead at any time
- Remote players' overhead chat is visible to all connected clients
