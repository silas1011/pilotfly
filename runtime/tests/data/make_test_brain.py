from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

STATE_SIZE = 4
CHANNELS = 32


def build(frame_planes):
    frame = helper.make_tensor_value_info("frame", TensorProto.FLOAT, [1, frame_planes, 64, 128])
    state = helper.make_tensor_value_info("state", TensorProto.FLOAT, [1, STATE_SIZE])
    channels = helper.make_tensor_value_info("channels", TensorProto.FLOAT, [1, CHANNELS])
    state_out = helper.make_tensor_value_info("state_out", TensorProto.FLOAT, [1, STATE_SIZE])

    initializers = [
        numpy_helper.from_array(np.array(2.0, dtype=np.float32), "two"),
        numpy_helper.from_array(np.array(1.0, dtype=np.float32), "one"),
        numpy_helper.from_array(np.array(-1.0, dtype=np.float32), "minus_one"),
        numpy_helper.from_array(np.array([0], dtype=np.int64), "first"),
        numpy_helper.from_array(np.array([1, CHANNELS], dtype=np.int64), "channels_shape"),
    ]

    nodes = [
        helper.make_node("ReduceMean", ["frame"], ["mean"], keepdims=0),
        helper.make_node("Mul", ["mean", "two"], ["doubled"]),
        helper.make_node("Sub", ["doubled", "one"], ["centred"]),
        helper.make_node("Gather", ["state", "first"], ["state_first"], axis=1),
        helper.make_node("Add", ["state_first", "centred"], ["raw"]),
        helper.make_node("Clip", ["raw", "minus_one", "one"], ["clipped"]),
        helper.make_node("Expand", ["clipped", "channels_shape"], ["channels"]),
        helper.make_node("Add", ["state", "one"], ["state_out"]),
    ]

    graph = helper.make_graph(nodes, "pilotfly_test_brain", [frame, state], [channels, state_out], initializers)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 9
    helper.set_model_props(model, {"pilotfly_rate_hz": "50"})
    onnx.checker.check_model(model)
    return model


def main():
    folder = Path(__file__).resolve().parent
    onnx.save(build(1), folder / "test_brain.onnx")
    onnx.save(build(3), folder / "wrong_frame_brain.onnx")


if __name__ == "__main__":
    main()
