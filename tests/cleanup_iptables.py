# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import infra.partitions
import sys

from loguru import logger as LOG

if __name__ == "__main__":
    LOG.remove()
    LOG.add(
        sys.stdout,
        format="<green>[{time:HH:mm:ss.SSS}]</green> {message}",
    )

    infra.partitions.Partitioner.dump()
    if len(sys.argv) <= 1 or sys.argv[1] not in ["-d", "--dump"]:
        infra.partitions.Partitioner.cleanup()
