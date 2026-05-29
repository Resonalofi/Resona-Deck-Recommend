from concurrent.futures import ThreadPoolExecutor

from fastapi import Request

from app.services.cache import MasterdataCache


def get_cache(request: Request) -> MasterdataCache:
    return request.app.state.cache


def get_pool(request: Request) -> ThreadPoolExecutor:
    return request.app.state.pool
