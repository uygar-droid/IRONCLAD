# Installing IRONCLAD (VST3)

IRONCLAD is a VST3 plugin for **Windows 10/11 (64-bit)** and **macOS 13 or newer**
(Apple Silicon and Intel). Any DAW that supports VST3 can host it; REAPER is the reference host.

> **Current build: 0.1.x (Milestone 1, foundation).** It only contains Input/Output gain and
> a temporary editor. It exists to prove that the plugin installs, loads, passes audio and
> automates correctly. The amp engine, cabinet, effects and the final interface arrive in
> later milestones (see ROADMAP.md).

## Windows

1. Unzip `IRONCLAD-windows-x64.zip`.
2. Copy the whole **`IRONCLAD.vst3`** folder (not only the file inside it) to **one** of:
   - `C:\Program Files\Common Files\VST3\` (all users; needs administrator rights), or
   - `%LOCALAPPDATA%\Programs\Common\VST3\` (current user only; paste this into the Explorer
     address bar; create the folder if it does not exist).
3. Start your DAW and rescan plugins.
   - **REAPER:** Options → Preferences → Plug-ins → VST → *Re-scan*. Then insert
     *VST3: IRONCLAD (Ironclad Audio)* on a track.

## macOS

1. Unzip `IRONCLAD-macos-universal.zip`.
2. Copy **`IRONCLAD.vst3`** to `~/Library/Audio/Plug-Ins/VST3/`
   (Finder → Go → Go to Folder…, paste the path; create the `VST3` folder if needed).
3. Builds before 1.0 are not yet signed with an Apple Developer ID, so macOS quarantines them.
   Remove the quarantine flag once in Terminal:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/IRONCLAD.vst3
   ```
   If you installed into the system folder `/Library/Audio/Plug-Ins/VST3/` instead, use
   `sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/VST3/IRONCLAD.vst3`
   (paste the whole line; macOS asks for your password).
   If REAPER scanned before you did this, use *Clear cache/re-scan*, not just *Re-scan*.
4. Start your DAW and rescan plugins (REAPER: Preferences → Plug-ins → VST → *Re-scan*).

## Using it with a guitar

- Put IRONCLAD on a **mono** track fed by your audio-interface input, or on a stereo track
  (the two channels are averaged so the level does not jump).
- Arm the track and enable input monitoring.
- Use the smallest audio buffer your interface handles reliably (64–256 samples) for low latency.

## Uninstall

Delete `IRONCLAD.vst3` from the folder you copied it to.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Not listed after rescan | Check you copied the `IRONCLAD.vst3` **folder** to a VST3 folder your DAW scans; in REAPER use *Clear cache/re-scan*. |
| macOS: "cannot be opened" / plugin fails to load | Run the `xattr` command above, then rescan. |
| No sound | Confirm the track's input and monitoring; IRONCLAD's Output is not at −60 dB. |
