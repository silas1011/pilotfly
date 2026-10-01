import argparse
import re
from pathlib import Path

import numpy as np
import pandas as pd
import requests

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "data"
CIRCUIT_PATH = ROOT / "assets" / "flight_circuit.npz"

CONNECTIVITY_URL = "https://raw.githubusercontent.com/philshiu/Drosophila_brain_model/main/Connectivity_783.parquet"
ANNOTATIONS_URL = (
    "https://raw.githubusercontent.com/flyconnectome/flywire_annotations/main/"
    "supplemental_files/Supplemental_file1_neuron_annotations.tsv"
)

LPTC_PATTERN = re.compile(r"^(HS[NES]|VS\d+|VST\d|VSm|H1|H2|LPT.*)$")
MIN_SYNAPSES = 5

ROLE_LPTC = 0
ROLE_INTER = 1
ROLE_DN = 2

DIRECTION_A = 0
DIRECTION_B = 1
DIRECTION_C = 2
DIRECTION_D = 3


def download(url: str, path: Path) -> Path:
    if path.exists():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    with requests.get(url, stream=True, timeout=60) as response:
        response.raise_for_status()
        with open(path, "wb") as file:
            for chunk in response.iter_content(chunk_size=1 << 20):
                file.write(chunk)
    return path


def load_tables(data_dir: Path = DATA_DIR) -> tuple[pd.DataFrame, pd.DataFrame]:
    connectivity = pd.read_parquet(download(CONNECTIVITY_URL, data_dir / "Connectivity_783.parquet"))
    annotations = pd.read_csv(
        download(ANNOTATIONS_URL, data_dir / "neuron_annotations.tsv"), sep="\t", low_memory=False
    )
    connections = pd.DataFrame({
        "pre": connectivity["Presynaptic_ID"].to_numpy(),
        "post": connectivity["Postsynaptic_ID"].to_numpy(),
        "count": connectivity["Connectivity"].to_numpy(),
        "sign": connectivity["Excitatory"].to_numpy(),
    })
    neurons = pd.DataFrame({
        "root_id": annotations["root_id"].to_numpy(),
        "super_class": annotations["super_class"].astype(str).to_numpy(),
        "cell_type": annotations["cell_type"].to_numpy(),
        "side": annotations["side"].astype(str).to_numpy(),
    })
    return connections, neurons


def allowed_directions(cell_type: str) -> list[int]:
    if cell_type.startswith("HS"):
        return [DIRECTION_A]
    if cell_type in ("H1", "H2"):
        return [DIRECTION_B]
    if re.match(r"^VS\d+$", cell_type):
        return [DIRECTION_D]
    return [DIRECTION_A, DIRECTION_B, DIRECTION_C, DIRECTION_D]


def build_circuit(connections: pd.DataFrame, neurons: pd.DataFrame, min_synapses: int = MIN_SYNAPSES) -> dict:
    neurons = neurons.drop_duplicates("root_id").copy()
    cell_type = neurons["cell_type"].where(neurons["cell_type"].notna(), None)
    neurons["type_name"] = [
        str(name) if name is not None else f"untyped_{super_class}"
        for name, super_class in zip(cell_type, neurons["super_class"])
    ]

    in_connectome = set(connections["pre"]) | set(connections["post"])
    neurons = neurons[neurons["root_id"].isin(in_connectome)]
    lptc = set(neurons.loc[neurons["type_name"].map(lambda name: bool(LPTC_PATTERN.match(name))), "root_id"])
    descending = set(neurons.loc[neurons["super_class"] == "descending", "root_id"])

    total_input = connections.groupby("post")["count"].sum()
    strong = connections[connections["count"] >= min_synapses]

    direct = strong[strong["pre"].isin(lptc) & strong["post"].isin(descending)]
    lptc_targets = set(strong.loc[strong["pre"].isin(lptc), "post"]) - lptc - descending
    into_descending = strong[strong["post"].isin(descending)]
    intermediates = lptc_targets & set(into_descending["pre"])
    reached = set(direct["post"]) | set(into_descending.loc[into_descending["pre"].isin(intermediates), "post"])

    members = lptc | intermediates | reached
    table = neurons[neurons["root_id"].isin(members)].copy()
    role = np.full(len(table), ROLE_INTER)
    role[table["root_id"].isin(lptc).to_numpy()] = ROLE_LPTC
    role[table["root_id"].isin(reached).to_numpy()] = ROLE_DN
    table["role"] = role
    table = table.sort_values(["role", "type_name", "side", "root_id"]).reset_index(drop=True)
    index = {root_id: i for i, root_id in enumerate(table["root_id"])}

    edges = strong[strong["pre"].isin(index.keys()) & strong["post"].isin(index.keys())]
    pre = edges["pre"].map(index).to_numpy()
    post = edges["post"].map(index).to_numpy()
    fraction = edges["count"].to_numpy() / total_input.loc[edges["post"]].to_numpy()
    weight = (edges["sign"].to_numpy() * fraction).astype(np.float32)

    type_names = sorted(table["type_name"].unique())
    type_index = {name: i for i, name in enumerate(type_names)}

    lptc_rows = table[table["role"] == ROLE_LPTC]
    direction_mask = np.zeros((len(lptc_rows), 4), dtype=np.float32)
    for row, name in enumerate(lptc_rows["type_name"]):
        direction_mask[row, allowed_directions(name)] = 1.0

    return {
        "root_id": table["root_id"].to_numpy().astype(np.int64),
        "role": table["role"].to_numpy().astype(np.int64),
        "side_left": (table["side"] == "left").to_numpy(),
        "neuron_type": table["type_name"].map(type_index).to_numpy().astype(np.int64),
        "type_names": np.array(type_names),
        "edge_pre": pre.astype(np.int64),
        "edge_post": post.astype(np.int64),
        "edge_weight": weight,
        "edge_count": edges["count"].to_numpy().astype(np.int64),
        "lptc_direction_mask": direction_mask,
        "min_synapses": np.int64(min_synapses),
    }


def describe(circuit: dict) -> str:
    role = circuit["role"]
    return (
        f"{len(role)} neurons ({int((role == ROLE_LPTC).sum())} tangential cells, "
        f"{int((role == ROLE_INTER).sum())} intermediate, {int((role == ROLE_DN).sum())} descending), "
        f"{len(circuit['edge_weight'])} connections, {len(circuit['type_names'])} cell types"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", default=str(CIRCUIT_PATH))
    parser.add_argument("--min-synapses", type=int, default=MIN_SYNAPSES)
    args = parser.parse_args()
    connections, neurons = load_tables()
    circuit = build_circuit(connections, neurons, args.min_synapses)
    np.savez_compressed(args.out, **circuit)
    print(f"Saved {args.out}: {describe(circuit)}")


if __name__ == "__main__":
    main()
