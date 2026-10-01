import pandas as pd

from pilotfly_train.flywire import ROLE_DN, ROLE_INTER, ROLE_LPTC, build_circuit


def fake_tables():
    neurons = pd.DataFrame({
        "root_id": [1, 2, 3, 4, 5, 6, 7],
        "super_class": ["optic", "optic", "central", "central", "descending", "descending", "descending"],
        "cell_type": ["HSN", "VS1", "relay", None, "DNa02", "DNg02_a", "DNx"],
        "side": ["left", "right", "left", "left", "left", "right", "left"],
    })
    connections = pd.DataFrame({
        "pre": [1, 1, 3, 2, 4, 1, 3],
        "post": [5, 3, 6, 4, 6, 7, 1],
        "count": [10, 6, 8, 5, 3, 2, 5],
        "sign": [1, 1, -1, 1, 1, 1, -1],
    })
    return connections, neurons


def test_circuit_keeps_tangential_cells_and_strong_paths():
    circuit = build_circuit(*fake_tables(), min_synapses=5)
    ids = list(circuit["root_id"])
    roles = dict(zip(ids, circuit["role"]))
    assert roles[1] == ROLE_LPTC and roles[2] == ROLE_LPTC
    assert roles[3] == ROLE_INTER
    assert roles[5] == ROLE_DN and roles[6] == ROLE_DN
    assert 4 not in ids
    assert 7 not in ids


def test_circuit_weights_are_signed_input_fractions():
    circuit = build_circuit(*fake_tables(), min_synapses=5)
    ids = list(circuit["root_id"])
    weights = {
        (ids[pre], ids[post]): weight
        for pre, post, weight in zip(circuit["edge_pre"], circuit["edge_post"], circuit["edge_weight"])
    }
    assert abs(weights[(1, 5)] - 1.0) < 1e-6
    assert abs(weights[(3, 6)] + 8.0 / 11.0) < 1e-6
    assert abs(weights[(3, 1)] + 1.0) < 1e-6
    assert (2, 4) not in weights


def test_direction_mask_follows_cell_type():
    circuit = build_circuit(*fake_tables(), min_synapses=5)
    ids = list(circuit["root_id"])
    mask = circuit["lptc_direction_mask"]
    assert list(mask[ids.index(1)]) == [1.0, 0.0, 0.0, 0.0]
    assert list(mask[ids.index(2)]) == [0.0, 0.0, 0.0, 1.0]
