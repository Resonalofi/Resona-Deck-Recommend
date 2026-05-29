from fastapi import Header, HTTPException, status

from app.core.config import settings


async def require_secret(x_resona_secret: str | None = Header(default=None)) -> None:
    if x_resona_secret != settings.resona_secret:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Access Denied",
        )
