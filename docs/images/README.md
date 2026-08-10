# Screenshots and media

Put photos and diagrams for the GitHub README (and other docs) here.

## Naming

Use short, stable names, for example:

- `face.jpg` — watch face
- `settings.jpg` — watch settings
- `companion.png` — phone app main screen
- `architecture.png` — diagram

## GitHub markdown

From the repo root `README.md`:

```markdown
![Alt text describing the photo](docs/images/face.jpg)
```

From a file under `docs/`:

```markdown
![Alt text](images/face.jpg)
```

Tips:

- Prefer **JPEG** for photos, **PNG** for UI captures with text/sharp edges.
- Resize before commit (e.g. max width ~1200 px) so the README stays fast.
- Always use **relative** paths from the markdown file; GitHub will not resolve local `C:\…` paths.
- After pushing, hard-refresh the GitHub page if an image looks cached/stale.

The HTML comment block near the top of `README.md` has ready-to-uncomment examples once files exist.
