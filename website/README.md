# IrAutoX Website 2.0.2

The website includes a small Flask backend that speaks the same IrAutoX TCP protocol as the Qt launcher. It uses the production server at `irautox.ir:6768` by default, using the launcher's 4-byte big-endian JSON framing. Browser cookies contain only an opaque session id; IrAutoX credentials stay in the Python server process memory and expire automatically.

## Run the full website

From the repository root:

```bash
python -m pip install -r website/requirements.txt
python website/server.py
```

Open `http://127.0.0.1:8080/`.

Optional environment variables:

```text
IRAUTOX_HOST=irautox.ir
IRAUTOX_PORT=6768
IRAUTOX_WEB_HOST=127.0.0.1
IRAUTOX_WEB_PORT=8080
IRAUTOX_WEB_SECRET=<long-random-production-secret>
IRAUTOX_COOKIE_SECURE=1
```

Set `IRAUTOX_COOKIE_SECURE=1` when production is served over HTTPS and set a stable, secret `IRAUTOX_WEB_SECRET` before using multiple workers or restarts.

## API

- `POST /api/login` authenticates with the IrAutoX server and creates a server-side website session.
- `POST /api/logout` clears it.
- `GET /api/me` returns the current public account object.
- `GET /api/games` logs the session into the IrAutoX TCP server and requests `get_games`.

Play buttons call `irautox://launch/<game-id>`. If the protocol does not take over, the page shows the Launcher download prompt, so installation and game execution remain in the Windows app.

## Static-only mode

For UI-only development you can still run from the repository root:

```bash
python -m http.server 8080
```

Then open `http://localhost:8080/website/`. In static-only mode the account API is unavailable and Store falls back to `website/games.json`.
