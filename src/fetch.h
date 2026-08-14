#pragma once

#include "config.h"

#include <string>

std::string fetchWithFallback(const FetchSource& source, const std::string& name);
