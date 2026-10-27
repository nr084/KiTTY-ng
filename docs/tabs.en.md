# Session tabs — Windows and Linux

[Documentation](README.en.md) · [Русский](tabs.md)

In KiTTY-ng 0.85-7, **Duplicate Session** opens an independent connection in a new tab in the same window. A single session has no visible tab bar; the bar appears at the top when a second session is added.

- The active tab has a blue background. Long titles are ellipsized.
- Drag tabs to reorder them, or right-click a tab and choose **Move left / Move right**.
- The red circular close button on each tab and its **Close tab** menu item close that session without confirmation, even when session close confirmation is enabled. Other connections remain open.
- Closing the entire window still respects the session confirmation setting.
- The tab bar hides again when only one session remains.

## Disable tabs

Tabs are enabled by default. To duplicate into separate windows, set this in the portable directory’s `kitty.ini` before opening a new window:

```ini
[KiTTY]
SessionTabs=0
```

Use `SessionTabs=1` to enable tabs. A missing setting also means enabled. Saved session files retain their existing format.

macOS tabs require a separate native implementation and validation on a Mac; this implementation covers Windows and Linux.

![Windows tabs](screenshots/windows-tabs-en.png)

![Linux tabs](screenshots/linux-tabs-en.png)
