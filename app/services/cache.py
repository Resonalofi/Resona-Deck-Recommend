import asyncio

from app.constants import REQUIRED_MASTERDATA_KEYS
from app.core.config import Settings
from app.enums import Server
from app.services.fetch import fetch_with_fallback


class MasterdataCache:

    def __init__(self, settings: Settings):
        self._settings = settings
        self._lock = asyncio.Lock()
        self._generations: dict[Server, int] = {server: 0 for server in Server}

    def generation_for(self, server: Server) -> int:
        return self._generations[server]


    async def get_master_bytes(self, server: Server) -> dict[str, bytes]:
        async with self._lock:
            return {
                key: await fetch_with_fallback(self._settings.source_for(server, key), key)
                for key in REQUIRED_MASTERDATA_KEYS
            }


    async def get_musicmetas_bytes(self, server: Server) -> bytes:
        async with self._lock:
            return await fetch_with_fallback(self._settings.musicmeta_source_for(server), "music_metas")


    async def get_event_cards_bytes(self, server: Server) -> bytes:
        async with self._lock:
            return await fetch_with_fallback(self._settings.source_for(server, "eventCards"), "eventCards")


    async def reload(self, server: Server) -> None:
        async with self._lock:
            self._generations[server] += 1
