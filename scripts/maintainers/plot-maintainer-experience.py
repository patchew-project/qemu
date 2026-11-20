#!/usr/bin/env python3
#
# plot-maintainer-experience.py: plot the experience of the current
# maintainer pool
#
# Copyright (C) 2026 Linaro, Ltd.
#
# SPDX-License-Identifier: GPL-2.0-or-later

"""
Plot the distribution of experience across the current maintainers,
using the CSV from get_maintainer.py --list-maintainers:

    years as a maintainer       now - Maintainer Since
    experience when appointed   Maintainer Since - Contributor Since

"Contributor Since" is the first commit authored or committed, or the
first Signed-off-by, Co-developed-by, Reviewed-by, Tested-by or Acked-by
trailer.  Maintainers added within ARRIVED_DAYS of that usually came with
their own code (a new board, device or port) and are shaded apart.
Individuals are not labelled; those left out are listed on stdout.
"""

import argparse
import os
import sys

import matplotlib
if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
    matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

REQUIRED_COLUMNS = {"Maintainer Name", "Contributor Since",
                    "Maintainer Since"}
# added to MAINTAINERS this soon after first involvement: arrived with
# their code rather than being appointed after contributing
ARRIVED_DAYS = 30
COLOR = "#2a78d6"
ARRIVED_COLOR = "#a9c9f0"


def years_between(start, end):
    return ((end - start).dt.days / 365.25).dropna()


def plot_panel(ax, years, title, xlabel, bins, color, arrived=None):
    if arrived is None:
        ax.hist(years, bins=bins, color=color, edgecolor="white",
                linewidth=1.5)
    else:
        # all the arrivals fall in the first bin, stacked under the rest
        ax.hist([years[arrived], years[~arrived]], bins=bins, stacked=True,
                color=[ARRIVED_COLOR, color], edgecolor="white",
                linewidth=1.5,
                label=[f"Arrived with their code "
                       f"(within {ARRIVED_DAYS} days): {arrived.sum()}",
                       "Appointed after contributing"])
        ax.legend(frameon=False, fontsize=9, loc="upper right")

    median = years.median()
    ax.axvline(median, color="0.2", linewidth=1)
    ax.annotate(f"median {median:.1f}", (median, 1), xycoords=("data",
                "axes fraction"), xytext=(4, -4), textcoords="offset points",
                ha="left", va="top", fontsize=9, color="0.2")

    title = (f"{title}  (n={len(years)}, mean {years.mean():.1f}, "
             f"skew {years.skew():+.2f})")
    ax.set_title(title, fontsize=11, loc="left")
    ax.set_xlabel(xlabel, fontsize=10)
    ax.set_ylabel("Maintainers", fontsize=10)
    ax.grid(axis="y", linestyle="--", alpha=0.5)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def plot_experience(df, now, title):
    contributor = pd.to_datetime(df["Contributor Since"])
    maintainer = pd.to_datetime(df["Maintainer Since"])

    tenure = years_between(maintainer, pd.Series(now, index=df.index))
    before = years_between(contributor, maintainer)
    # no involvement at all, or a maintainer entry before it: someone
    # else's patch added them, or MAINTAINERS doesn't match git
    left_out = df.loc[~df.index.isin(before[before >= 0].index)]
    if not left_out.empty:
        print(f"Left out of the experience panel ({len(left_out)}): "
              "no matching involvement, or in MAINTAINERS before it")
        for _, row in left_out.iterrows():
            first = row["Contributor Since"]
            print(f"  {row['Maintainer Name']}: maintainer "
                  f"{row['Maintainer Since']}, first involvement "
                  f"{first if pd.notna(first) else 'none'}")
    before = before[before >= 0]
    arrived = (maintainer - contributor).dt.days[before.index] <= ARRIVED_DAYS

    top = int(np.ceil(max(tenure.max(), before.max())))
    bins = np.arange(0, top + 1)

    fig, (upper, lower) = plt.subplots(2, 1, figsize=(10, 7.5), sharex=True)
    plot_panel(upper, tenure, "Years as a maintainer",
               f"Years before {now:%Y-%m-%d}", bins, "#eb6834")
    plot_panel(lower, before, "Experience when becoming a maintainer",
               "Years from first involvement (commit or trailer) to "
               "entering MAINTAINERS", bins, COLOR, arrived)
    # sharex hides the upper tick labels, but its xlabel still applies
    upper.tick_params(labelbottom=True)
    lower.set_xlim(0, top)
    lower.set_xticks(bins[::2])

    fig.suptitle(title or "Experience of the Current QEMU Maintainers",
                 fontsize=14, fontweight="bold")
    fig.tight_layout()
    return fig


def build_parser():
    parser = argparse.ArgumentParser(
        description="Plot the experience distribution of current maintainers.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  scripts/get_maintainer.py --list-maintainers > maintainers.csv
  %(prog)s -i maintainers.csv -o maintainer-experience.png
""",
    )
    parser.add_argument("-i", "--input", required=True,
                        help="CSV from get_maintainer.py --list-maintainers")
    parser.add_argument("-o", "--output", default=None,
                        help="Output image file. If not specified, "
                             "displays interactively.")
    parser.add_argument("--title", default=None, help="Plot title")
    return parser


def main():
    args = build_parser().parse_args()

    try:
        df = pd.read_csv(args.input)
    except (OSError, pd.errors.ParserError) as e:
        print(f"Error reading '{args.input}': {e}", file=sys.stderr)
        sys.exit(1)

    missing = REQUIRED_COLUMNS - set(df.columns)
    if missing:
        print(f"Error: '{args.input}' lacks columns: {', '.join(missing)}",
              file=sys.stderr)
        sys.exit(1)

    now = pd.Timestamp.now().normalize()
    fig = plot_experience(df, now, args.title)
    if args.output:
        fig.savefig(args.output, dpi=300)
        print(f"Plot saved to {args.output}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
