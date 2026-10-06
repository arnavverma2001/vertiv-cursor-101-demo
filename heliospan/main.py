"""HelioSpan Monitor application entrypoint."""

import asyncio
import contextlib
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from heliospan import __version__
from heliospan.api.routes import router
from heliospan.config import TICK_SECONDS
from heliospan.services.store import FleetStore

logger = logging.getLogger(__name__)
STATIC_DIR = Path(__file__).resolve().parent / "static"
_NO_CACHE = {"Cache-Control": "no-cache"}


class NoCacheStatic(StaticFiles):
    """Avoid a stale dashboard while someone is editing the UI live."""

    async def get_response(self, path: str, scope):  # type: ignore[no-untyped-def]
        response = await super().get_response(path, scope)
        response.headers["Cache-Control"] = "no-cache"
        return response


def create_app(*, run_simulator: bool = True) -> FastAPI:
    @asynccontextmanager
    async def lifespan(app: FastAPI):
        store = FleetStore.seed()
        store.tick()
        app.state.store = store
        stop = asyncio.Event()
        task: asyncio.Task[None] | None = None
        if app.state.run_simulator:
            task = asyncio.create_task(_simulator_loop(store, stop))
        try:
            yield
        finally:
            stop.set()
            if task is not None:
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task

    app = FastAPI(
        title="HelioSpan Monitor",
        summary="Power and thermal monitoring for critical facilities.",
        version=__version__,
        lifespan=lifespan,
    )
    app.state.run_simulator = run_simulator
    app.include_router(router)
    app.mount("/assets", NoCacheStatic(directory=STATIC_DIR), name="assets")

    @app.get("/", include_in_schema=False)
    def index() -> FileResponse:
        return FileResponse(STATIC_DIR / "index.html", headers=_NO_CACHE)

    @app.get("/favicon.svg", include_in_schema=False)
    def favicon() -> FileResponse:
        return FileResponse(STATIC_DIR / "favicon.svg", media_type="image/svg+xml", headers=_NO_CACHE)

    return app


async def _simulator_loop(store: FleetStore, stop: asyncio.Event) -> None:
    while not stop.is_set():
        try:
            await asyncio.wait_for(stop.wait(), timeout=TICK_SECONDS)
        except TimeoutError:
            try:
                store.tick()
            except Exception:
                logger.exception("simulator tick failed")


app = create_app()
