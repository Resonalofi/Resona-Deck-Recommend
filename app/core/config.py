import tomllib

from pathlib import Path

from pydantic import BaseModel, Field, field_validator

from app.enums import Server


class FetchSource(BaseModel):
    location: str
    fallback: list[str] = Field(default_factory=list)


class MasterdataServer(FetchSource):
    musicmeta: str
    musicmeta_fallback: list[str] = Field(default_factory=list)
    masterdata_override: bool = False


    @property
    def musicmeta_source(self) -> FetchSource:
        return FetchSource(location=self.musicmeta, fallback=self.musicmeta_fallback)


class RuntimeSettings(BaseModel):
    host: str = "127.0.0.1"
    port: int = 23457


class AuthSettings(BaseModel):
    resona_secret: str


class WorkerSettings(BaseModel):
    pool_size: int = 1
    default_timeout_ms: int = 8000
    wl_timeout_ms: int = 12000


class Settings(BaseModel):
    runtime: RuntimeSettings = Field(default_factory=RuntimeSettings)
    auth: AuthSettings
    worker: WorkerSettings = Field(default_factory=WorkerSettings)
    masterdata: dict[Server, MasterdataServer]
    wl_support: dict[str, FetchSource]

    @property
    def host(self) -> str:
        return self.runtime.host

    @property
    def port(self) -> int:
        return self.runtime.port

    @property
    def resona_secret(self) -> str:
        return self.auth.resona_secret

    def source_for(self, server: Server, key: str, wl_ver: int) -> FetchSource:
        if key == "worldBloomSupportDeckBonuses" and wl_ver == 1:
            return self.wl_support["wl1"]
        if key == "worldBloomSupportDeckBonuses" and wl_ver == 2:
            return self.wl_support["wl2"]
        if key == "honors":
            return self.masterdata[server]
        if self.masterdata[Server.jp].masterdata_override:
            return self.masterdata[Server.jp]
        return self.masterdata[server]


def load_settings() -> Settings:
    with Path("config.toml").open("rb") as f:
        return Settings.model_validate(tomllib.load(f))


settings = load_settings()
