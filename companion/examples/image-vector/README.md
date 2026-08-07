# Image Vector (JS sub-app)

Full-screen **vector** approximation of the same logo as `examples/image`.

| App | Encoding | On-face size |
|---|---|---|
| **Image** (`slate.image`) | One RGB332 `PATCH` | 63×63 centred (4 KiB bitmap ceiling) |
| **Image Vector** (`slate.image.vector`) | Filled `CIRCLE`s only | Full 240×240 |

Same 4 KiB display-list budget; this one spends it on geometry instead of pixels.

`preview_compare.png` is source vs the circle stack used here (desktop preview).

## Install

```powershell
Compress-Archive -Path manifest.json,main.js -DestinationPath slate.image.vector.zip -Force
```

Open the zip with the Slate companion (SideloadActivity). It appears as **Image Vector**.
Swipe right to leave. Sideload **Image** the same way to compare both.
