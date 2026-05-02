
## Overview
When the player joins the server, they will receive a modal with a few short steps to ensure their character is ready to play.

When a player is in this "just joined" state - other players can see their edits on their character's appearance in real-time. As for the player's name - they will start with their pre-defined name and then if they update their name, their name should update across their context menu, chat log, etc.

- **Player Name**: At the top of the modal is an editable text box with their pre-determined name as the default starting value. Players can edit/rename their name as they please but they can't leave it blank. When testing offline, ensure that the player has a default name fallback. Character limit of 20 characters. 
- **Shirt Color**: Underneath that is four squares with colors filled in; Red, Blue, Yellow, Green. Selecting one will update the player's shirt color.
- **Skin Tone**: Underneath that is four squares with colors filled in; Fair, Tan, Olive, Brown. Selecting one will update the player's skin color.
- Underneath that will be a button to "Join" - selecting that will lock in the player's changes and their player avatar as well as their name in all instances of the UI should reflect their final changes to both themselves and other players.
- Once a player has selected "Join" log "[PlayerName] has joined the server. Welcome!" to the server chat.

## Appearance
- Use the same style/themeing as the UI Panel.
- Darken the game world behind the modal until changes are saved and join is pressed.