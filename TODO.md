# TODO log

Deferred items found during console runs. Newest first. Remove an item when it is fixed.

## Downloads

- Details size estimates sum every Windows/English depot (140: 610 MB estimated, 87 MB installed); use the licensed depots, as the installer does.
- Chunks download one at a time over a new TLS connection each; add bounded parallelism and connection reuse.

- Free space is not checked before installing on the console: `statfs`/`statvfs` terminate the title there. Find a permitted way to read free space (e.g. a system service) and restore the pre-check.
- The console's `readdir` returned no entries: the install service keeps a registry and installs write a file list used for removal; a directory walk remains only as a fallback for files the list does not cover.

- Depot manifests larger than 16 MiB (the HTTP client's response cap) cannot be downloaded yet; very large games need a streamed or larger-cap manifest fetch.

## Cosmetic and UX

- Downloads: live job rows show "0.0 GB / 0.0 GB" instead of the job's done/total bytes, and the job action labels are raw keys ("Job resume 242820"); the empty state still says "simulated transfer".

- **Text is small for a TV.** Fonts across Home, Library, Details, Downloads and Settings read small from couch distance; raise the type scale for 10-foot viewing (user feedback, 2026-10-05).
- Details: the old "Install and launch are unavailable." note overlaps the More button now that live install rows exist; hide it when an install row is shown.
- Details: after app info arrives, focus stays on Favorite instead of moving to Install.
- Downloads: a failed job shows the stage ("Download") in red but not its failure text; show the job's reason.
- Downloads: the Storage panel is empty in live mode; show the install volume's free space.
- Some covers stay placeholders (for example Proteus): titles without a `library_600x900` portrait on the Steam CDN. Consider a header-image fallback.
