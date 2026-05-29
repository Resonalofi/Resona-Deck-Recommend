from concurrent.futures import ProcessPoolExecutor

from fastapi import Request

from app.services.cache import MasterdataCache


def get_cache(request: Request) -> MasterdataCache:
    return request.app.state.cache


def get_pool(request: Request) -> ProcessPoolExecutor:
    return request.app.state.pool
