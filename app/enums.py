from enum import Enum


class Server(str, Enum):
    jp = "jp"
    cn = "cn"
    tw = "tw"


class LiveType(str, Enum):
    multi = "multi"
    solo = "solo"
    auto = "auto"
    challenge = "challenge"


class CalTar(str, Enum):
    score = "score"
    bonus = "bonus"


class Algorithm(str, Enum):
    dfs = "dfs"
    ga = "ga"


class DefaultImage(str, Enum):
    original = "original"
    special_training = "special_training"
