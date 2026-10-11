"""Executable names shared by local build and release packaging."""

UTILITY_NAMES = {"plink": "klink", "pscp": "kscp", "psftp": "ksftp",
                 "puttygen": "kittygen", "pageant": "kageant"}


def main_executable(architecture):
    if architecture not in ("x64", "x86", "arm64"):
        raise ValueError("Unknown architecture: " + architecture)
    return f"kitty-ng-{architecture}.exe"


def utility_executable(program, architecture):
    main_executable(architecture)  # Validate the architecture.
    return f"{UTILITY_NAMES[program]}-{architecture}.exe"


def release_executables(architecture):
    return [main_executable(architecture)] + [
        utility_executable(program, architecture) for program in UTILITY_NAMES]
