import sys

import uvicorn

from app.core.config import settings


def main() -> None:
    uvicorn.run(
        "app.main:app",
        host=settings.host,
        port=settings.port,
        workers=settings.runtime.workers,
        loop="uvloop" if sys.platform != "win32" else "asyncio",
        log_level="info",
    )


if __name__ == "__main__":
    main()
