"""Bootstrap-only argument parsing with transparent product-argument forwarding."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from dataclasses import dataclass


@dataclass(frozen=True)
class BootstrapArguments:
    check_only: bool
    prepare_only: bool
    jobs: int | None
    product_arguments: tuple[str, ...]


def parse_arguments(argv: Sequence[str] | None = None) -> BootstrapArguments:
    parser = argparse.ArgumentParser(
        description="Provision, build, and launch Zelda3D.", add_help=False
    )
    bootstrap = parser.add_argument_group("bootstrap options")
    bootstrap.add_argument(
        "--bootstrap-check",
        action="store_true",
        help="validate dependencies and ROM discovery without building or launching",
    )
    bootstrap.add_argument(
        "--bootstrap-prepare-only",
        action="store_true",
        help="provision and build without launching the game",
    )
    bootstrap.add_argument(
        "--bootstrap-jobs",
        type=int,
        help="parallel build jobs (default: detected CPU count)",
    )
    bootstrap.add_argument(
        "--bootstrap-help", action="help", help="show bootstrap options and exit"
    )
    known, product_arguments = parser.parse_known_args(argv)
    if known.bootstrap_jobs is not None and known.bootstrap_jobs < 1:
        parser.error("--bootstrap-jobs must be positive")
    if product_arguments[:1] == ["--"]:
        product_arguments = product_arguments[1:]
    return BootstrapArguments(
        check_only=known.bootstrap_check,
        prepare_only=known.bootstrap_prepare_only,
        jobs=known.bootstrap_jobs,
        product_arguments=tuple(product_arguments),
    )
