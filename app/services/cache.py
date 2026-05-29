import asyncio

from app.constants import REQUIRED_MASTERDATA_KEYS
from app.core.config import FetchSource, Settings
from app.enums import Server
from app.services.fetch import fetch_with_fallback


class MasterdataCache:

    def __init__(self, settings: Settings):
        self._settings = settings
        self._bytes: dict[str, bytes] = {}
        self._lock = asyncio.Lock()


    async def _get_cached(self, server: Server, source: FetchSource, name: str) -> bytes:
        key = f"{server.value}/{name}"
        if key not in self._bytes:
            self._bytes[key] = await fetch_with_fallback(source, name)
        return self._bytes[key]


    async def get_master_bytes(self, server: Server, wl_ver: int) -> dict[str, bytes]:
        async with self._lock:
            return {
                key: await self._get_cached(server, self._settings.source_for(server, key, wl_ver), key)
                for key in REQUIRED_MASTERDATA_KEYS
            }


    async def get_musicmetas_bytes(self, server: Server) -> bytes:
        async with self._lock:
            return await self._get_cached(server, self._settings.musicmeta_source_for(server), "music_metas")


    async def reload(self, server: Server) -> None:
        async with self._lock:
            prefix = f"{server.value}/"
            for key in list(self._bytes):
                if key.startswith(prefix):
                    del self._bytes[key]
