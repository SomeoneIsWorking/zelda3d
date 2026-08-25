"""Runtime archive and asset linking for the built harness binary."""

from harness_build import HarnessBuild


def link_runtime_inputs(build: HarnessBuild) -> None:
    source = build.shipping_build_dir / "soh"
    destination = build.binary.parent
    destination.mkdir(parents=True, exist_ok=True)
    for name in ("oot.o2r", "soh.o2r", "assets"):
        target = source / name
        link = destination / name
        if not target.exists():
            continue
        if link.is_symlink():
            if link.resolve(strict=False) == target.resolve(strict=False):
                continue
            link.unlink()
        elif link.exists():
            continue
        link.symlink_to(target, target_is_directory=target.is_dir())
