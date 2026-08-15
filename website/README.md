# IrAutoX Website

Run from the repository root so the website can also load the bundled launcher font and logo:

```bash
python -m http.server 8080
```

Open `http://localhost:8080/website/`.

The frontend expects optional HTTP endpoints at `/api/games` and `/api/login`. If `/api/games` is unavailable it falls back to `website/games.json`. Play buttons call `irautox://launch/<game-id>` and show the Windows launcher download when the protocol does not take over.

For production, serve the repository website assets under the main IrAutoX domain or copy `resources/Vazir.ttf` and `resources/logo.svg` into the deployed static asset directory and adjust the two relative URLs in `styles.css` and `index.html`.
