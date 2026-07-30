from ._cognator import abi_version, detect_file, version


def detect(path, **options):
    return detect_file(path, **options)


__all__ = ["abi_version", "detect", "detect_file", "version"]
