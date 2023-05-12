# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import os
import sys
from random import randrange, choice, choices
from itertools import combinations, count


def fully_connected_scenario(nodes, steps):
    index = count(start=1)
    step_def = {
        0: lambda: "dispatch_all",
        1: lambda: "periodic_all,{}".format(
            choices([randrange(20), randrange(100, 500)], weights=[10, 1])[0]
        ),
        2: lambda: f"replicate,latest,hello {next(index)}",
    }

    # Define the nodes
    lines = [f'nodes,{",".join(str(n) for n in range(nodes))}']

    lines.extend(
        f"connect,{first},{second}"
        for first, second in combinations(range(nodes), 2)
    )
    lines.extend(
        (
            "periodic_one,0,110",
            "dispatch_all",
            "periodic_all,30",
            "dispatch_all",
            "state_all",
        )
    )
    lines.extend(step_def[choice(list(step_def.keys()))]() for _ in range(steps))
    lines.append("state_all")

    # Allow the network to reconcile, and assert it reaches a stable state

    # It is likely this scenario has resulted in a lot of elections and very little commit advancement.
    # To reach a stable state, we need to give each node a chance to win an election and share their state.
    # In a real network, we expect this to arise from the randomised election timeouts, and it is sufficient
    # for one of a quorum of nodes to win and share their state. This exhaustive approach ensures convergence,
    # even in the pessimal case.
    for node in range(nodes):
        lines.extend(
            (
                f"periodic_one,{node},100",
                "dispatch_all",
                "replicate,latest,CommitConfirmer",
                "periodic_all,10",
                "dispatch_all",
                "periodic_all,10",
                "dispatch_all",
                "state_all",
            )
        )
    lines.append("assert_state_sync")

    return "\n".join(lines)


def generate_scenarios(tgt_dir="."):
    NODES = 3
    SCENARIOS = 3
    STEPS = 25

    scenario_paths = []
    for scen_index in range(SCENARIOS):
        with open(os.path.join(tgt_dir, f"scenario-{scen_index}"), "w", encoding="utf-8") as scen:
            scen.write(fully_connected_scenario(NODES, STEPS) + "\n")
            scenario_paths.append(os.path.realpath(scen.name))

    return scenario_paths


if __name__ == "__main__":
    tgt_dir = sys.argv[1]
    generate_scenarios(tgt_dir)
