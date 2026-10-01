import argparse
from pathlib import Path

import numpy as np


def extract(model_name: str) -> dict:
    from flyvis import NetworkView

    view = NetworkView(model_name)
    network = view.init_network()
    network.clamp()
    connectome = network.connectome
    params = network._param_api()

    type_names = [name.decode() for name in connectome.unique_cell_types[:]]
    type_index = {name: i for i, name in enumerate(type_names)}
    node_type = np.array([type_index[name.decode()] for name in connectome.nodes.type[:]])
    node_u = np.asarray(connectome.nodes.u[:])
    node_v = np.asarray(connectome.nodes.v[:])
    extent = int(max(np.abs(node_u).max(), np.abs(node_v).max()))
    size = 2 * extent + 1

    node_bias = params.nodes.bias[:].detach().cpu().numpy()
    node_tau = params.nodes.time_const[:].detach().cpu().numpy()
    bias = np.zeros(len(type_names), dtype=np.float32)
    tau = np.zeros(len(type_names), dtype=np.float32)
    mask = np.zeros((len(type_names), size, size), dtype=np.float32)
    for node in range(len(node_type)):
        bias[node_type[node]] = node_bias[node]
        tau[node_type[node]] = node_tau[node]
        mask[node_type[node], node_u[node] + extent, node_v[node] + extent] = 1.0

    source = np.asarray(connectome.edges.source_index[:])
    target = np.asarray(connectome.edges.target_index[:])
    weight = params.edges.weight[:].detach().cpu().numpy()
    du = node_u[target] - node_u[source]
    dv = node_v[target] - node_v[source]
    radius = int(max(np.abs(du).max(), np.abs(dv).max()))
    kernel = np.zeros((len(type_names), len(type_names), 2 * radius + 1, 2 * radius + 1), dtype=np.float32)
    kernel[node_type[target], node_type[source], radius - du, radius - dv] = weight

    input_types = [name.decode() for name in connectome.input_cell_types[:]]
    return {
        "type_names": np.array(type_names),
        "input_types": np.array(input_types),
        "bias": bias,
        "tau": tau,
        "mask": mask,
        "kernel": kernel,
        "extent": np.int64(extent),
        "source_model": np.array(model_name),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default="flow/0000/000")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "assets" / "flyvis_eye.npz"))
    args = parser.parse_args()
    data = extract(args.model)
    np.savez_compressed(args.out, **data)
    print(f"Saved {args.out}")


if __name__ == "__main__":
    main()
