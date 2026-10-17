# LAMEORG

### Because your Downloads folder is not a landfill. Probably.

Ever download one innocent little file, then blink three weeks later and discover 900 files, 47 ZIP archives, mystery PDFs, and something named `final_FINAL_reallyfinal.tar.gz`?

Yeah. We have a C program for that.

**LAMEORG** sorts visible files into folders named after their extensions. One command. Less chaos. More dignity.

## The magic trick

```bash
cd ~/Downloads
lameorg
```

WHAM BAM THANK YOU, MAAM.

Your files get sorted into folders like `pdf/`, `png/`, `zip/`, `c/`, and `tar.gz/`.

## Features

- Written in C. No Python runtime required for normal operation.
- Recognizes common compound extensions like `.tar.gz`.
- Creates extension folders automatically.
- Preserves original filenames.
- Never intentionally overwrites an existing destination.
- Leaves directories and hidden files alone.
- Handles files without recognized extensions by skipping them.
- `--dry-run` previews the chaos cleanup before anything moves.
- Records successful moves in a private-permissions undo manifest.
- `--undo FILE` attempts to restore the previous arrangement.

## Build it

```bash
make
make test
```

## Install it

Default installation:

```bash
sudo make install
```

Install directly into `/usr/bin` instead:

```bash
sudo make PREFIX=/usr install
```

## Use it responsibly

Preview first:

```bash
lameorg --dry-run
```

Organize:

```bash
lameorg
```

Undo a previous run using the manifest filename printed by LAMEORG:

```bash
lameorg --undo .lameorg-manifest-YOUR-FILENAME.tsv
```

Keep the manifest. It is your receipt from the cleanup operation.

## The ground rules

LAMEORG is not a file shredder, a backup system, or a wizard who can explain why you downloaded six copies of the same installer.

Review the dry run before organizing important directories. Undo can skip files when their original paths are occupied, and filesystem failures can interrupt a run.

## Why?

Because life is too short to manually drag 63 PDFs into a folder.

## License

MIT. See [LICENSE](LICENSE).

---

**LAMEORG — Organize first. Ask why you downloaded it later.**
