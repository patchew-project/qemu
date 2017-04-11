#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Generate a simple graph of flushes over time
#
# Author: Alex Bennée <alex.bennee@linaro.org>
#

import os
import simpletrace
import argparse
import numpy as np
import matplotlib.pyplot as plt

class CpuTLBFlushAnalyser(simpletrace.Analyzer):
    "A simpletrace Analyser for extracting flush stats."

    def __init__(self):
        self.stats = 0
        self.timestamps = []
        self.flush_self = []
        self.flush_async = []
        self.flush_synced = []


    def tlb_flush_stats(self, timestamp, flush_self, flush_async, flush_synced):
        "Match for tlb_flush_stats event. Appends counts to the relevant array."
        self.timestamps.append(timestamp)
        self.flush_self.append(flush_self)
        self.flush_async.append(flush_async)
        self.flush_synced.append(flush_synced)
        self.stats += 1


def get_args():
    "Grab options"
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", "-o", type=str, help="Render plot to file")
    parser.add_argument("events", type=str, help='trace file read from')
    parser.add_argument("tracefile", type=str, help='trace file read from')
    return parser.parse_args()

if __name__ == '__main__':
    args = get_args()

    # Gather data from the trace
    analyzer = CpuTLBFlushAnalyser()
    simpletrace.process(args.events, args.tracefile, analyzer)

#    x = np.arange(analyzer.stats)
    fself, = plt.plot(analyzer.timestamps, analyzer.flush_self, label="Self")
    fasync, = plt.plot(analyzer.timestamps, analyzer.flush_async, label="Async")
    fsynced, = plt.plot(analyzer.timestamps, analyzer.flush_synced, label="Synced")

    plt.legend(handles=[fself, fasync, fsynced])
    plt.xlabel("Execution Time")
    plt.ylabel("Culmlative count")

    if args.output:
        plt.savefig(args.output)
    else:
        plt.show()
