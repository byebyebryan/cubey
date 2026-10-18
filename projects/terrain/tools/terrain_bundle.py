"""Recoverable publication of generated directory bundles on the same filesystem.

Generation errors discard staging. Publication errors retain it. Cooperating
publishers serialize the short rename/rollback step through a persistent flock
file; readers must reopen bundles only after the generator returns successfully.
"""
from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
import fcntl
import os
from pathlib import Path
import shutil
import sys
import tempfile


def _directory(path: Path) -> None:
    if path.is_symlink() or not path.is_dir():
        raise ValueError(f"bundle must be a plain directory: {path}")


def _publish(prepared: Path, destination: Path) -> Path | None:
    _directory(prepared)
    lock = destination.with_name(f".{destination.name}.publish.lock")
    descriptor = os.open(lock, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, "rb") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        backup = None
        if destination.is_symlink() or (destination.exists() and not destination.is_dir()):
            raise ValueError(f"bundle destination must be a plain directory: {destination}")
        moved_previous = False
        try:
            if destination.exists():
                backup = Path(tempfile.mkdtemp(
                    prefix=f".{destination.name}.previous.", dir=destination.parent))
                os.replace(destination, backup)
                moved_previous = True
            os.replace(prepared, destination)
        except BaseException as publication_error:
            if backup is not None:
                # If the first rename failed before moving anything, this is
                # only our empty reservation. Never remove a nonempty backup.
                if not moved_previous and destination.exists():
                    try:
                        backup.rmdir()
                    except OSError:
                        publication_error.add_note(f"previous bundle location: {backup}")
                    raise
                try:
                    # Never replace a directory another writer put in the gap.
                    if destination.exists() or destination.is_symlink():
                        raise FileExistsError(f"destination appeared: {destination}")
                    os.replace(backup, destination)
                except BaseException as rollback_error:
                    raise RuntimeError(
                        f"bundle publication failed ({publication_error}); rollback failed "
                        f"({rollback_error}); previous bundle retained at {backup}"
                    ) from publication_error
            raise
        return backup


def _cleanup_published(path: Path, destination: Path) -> None:
    try:
        shutil.rmtree(path)
    except OSError as error:
        # Activation succeeded; keep that result and report cleanup separately.
        print(f"warning: published {destination}; could not remove {path}: {error}", file=sys.stderr)


@contextmanager
def staged_bundle(destination: Path, *, bundle_name: str | None = None) -> Iterator[Path]:
    """Yield a staging root; publish it (or one named child) only on success.

    Failed publication keeps the staging root and any un-restored previous
    bundle. Exceptions carry their recovery locations. SIGKILL/power loss may
    leave a hidden ``.<name>.previous.*`` sibling; it is never auto-deleted by
    a later attempt. This is a recoverable replacement, not a reader-atomic
    exchange or a power-loss durability guarantee.
    """
    destination = destination.absolute()
    if bundle_name is not None and (
        not bundle_name or Path(bundle_name).name != bundle_name or bundle_name in (".", "..")
    ):
        raise ValueError("bundle_name must be one directory name")
    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f"{destination.name}.tmp.", dir=destination.parent))
    try:
        yield staging
    except BaseException:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    else:
        prepared = staging if bundle_name is None else staging / bundle_name
        try:
            backup = _publish(prepared, destination)
        except BaseException as error:
            error.add_note(f"bundle recovery locations: staging root {staging}; destination {destination}")
            raise
        if backup is not None:
            _cleanup_published(backup, destination)
        if bundle_name is not None:
            _cleanup_published(staging, destination)
