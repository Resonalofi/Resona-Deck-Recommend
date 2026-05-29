import os
import aiohttp
import aiofiles

from app.core.config import FetchSource


async def fetch_with_fallback(source: FetchSource, name: str) -> bytes:
    try:
        async with aiofiles.open(os.path.join(source.location, f"{name}.json"), "rb") as f:
            return await f.read()
    except Exception as e:
        for url in source.fallback:
            try:
                async with aiohttp.ClientSession() as session:
                    async with session.get(f"{url}/{name}.json") as resp:
                        resp.raise_for_status()
                        data = await resp.read()
                os.makedirs(source.location, exist_ok=True)
                async with aiofiles.open(os.path.join(source.location, f"{name}.json"), "wb") as f:
                    await f.write(data)
                return data
            except Exception:
                continue
        raise e
