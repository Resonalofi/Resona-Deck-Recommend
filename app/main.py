import setproctitle
from contextlib import asynccontextmanager
from concurrent.futures import ThreadPoolExecutor

from fastapi import FastAPI
from fastapi.responses import ORJSONResponse

from app.api import api_router
from app.core.config import settings
from app.services.cache import MasterdataCache
from app.services.calc import DeckRecommendEngine


@asynccontextmanager
async def lifespan(app: FastAPI):
    setproctitle.setproctitle("Resona-Deck-Recommend")
    app.state.cache = MasterdataCache(settings)
    app.state.engine = DeckRecommendEngine()
    app.state.pool = ThreadPoolExecutor(max_workers=settings.worker.pool_size)
    try:
        yield
    finally:
        app.state.pool.shutdown(cancel_futures=True)


def create_app() -> FastAPI:
    app = FastAPI(
        title="Resona-Deck-Recommend",
        version="1.0.0",
        default_response_class=ORJSONResponse,
        lifespan=lifespan,
    )
    app.include_router(api_router)
    return app


app = create_app()
