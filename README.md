# OpenMower Discord History — Attachments

Binary attachments for the [OpenMowerBot](https://github.com/jeremysalwen/OpenMowerBot)
Discord history corpus. This repository holds the files themselves; the searchable
message corpus and the CLI live in the main repository.

## Why this is a separate repository

Attachments are large (hundreds of MB) and write-once. Keeping them here means:

- `git clone` of the main repository stays small and never downloads image data.
- The files are stored as ordinary Git objects, **not** Git LFS, so they are not
  subject to an LFS bandwidth budget.
- The static archive on GitHub Pages hotlinks them directly from
  `raw.githubusercontent.com`, so images render without bundling them into the
  1 GB Pages site limit.

## Layout

Files are stored as `<channelId>/<messageId>-<fileName>`, matching the
`localPath` suffix recorded in the main corpus (`data/attachments/` prefix removed).

The raw URL for an attachment is:

```
https://raw.githubusercontent.com/jeremysalwen/OpenMowerBot-attachments/main/<channelId>/<messageId>-<fileName>
```

## Do not use Git LFS here

This repository must stay on plain Git objects. Adding LFS tracking would
reintroduce the bandwidth budget that made attachments unusable in the main repo.

## MISSING.tsv

`MISSING.tsv` lists attachments that are referenced by the corpus but are not in
this repository. Their Discord CDN URLs have expired, and the only remaining
copies are Git LFS objects in the main repository, which are currently behind an
exhausted LFS budget. See the main repository's README for how to recover them.
