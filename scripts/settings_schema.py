# SPDX-License-Identifier: MIT
"""Devicetree-to-settings-schema adapter for high-resolution dial bindings."""

RECORD_SIZE = 24
RECORD_VERSION = 1
MODE_OFFSET = 1
MODE_SCROLL_BINDING = 1
MODE_ENCODER = 2
DIRECTION_SIZE = 11


def _write_binding(record, offset, binding):
    if binding is None:
        return
    identity, param1, param2 = binding
    record[offset:offset + 2] = [identity & 0xFF, identity >> 8]
    for index, param in enumerate((param1, param2)):
        record[offset + 2 + index * 4:offset + 6 + index * 4] = [
            (param >> shift) & 0xFF for shift in (0, 8, 16, 24)
        ]


def _record(context, binding, field):
    record = [RECORD_VERSION, 0] + [0] * (RECORD_SIZE - 2)
    if binding is None:
        return record

    behavior = getattr(binding, "controller", None)
    params = getattr(binding, "data", None)
    if behavior is None or not isinstance(params, dict):
        raise ValueError(f"{field}: expected a valid Behavior binding")
    param1, param2 = params.get("param1", 0), params.get("param2", 0)
    if any(type(param) is not int or not 0 <= param <= 0xFFFFFFFF
           for param in (param1, param2)):
        raise ValueError(f"{field}: behavior parameters must fit unsigned 32-bit values")

    labels = getattr(behavior, "labels", []) or []
    behavior_name = labels[0] if labels else getattr(behavior, "name", "")
    if behavior_name == "hires_dial_encoder":
        outputs = getattr(behavior, "props", {}).get("bindings")
        outputs = getattr(outputs, "val", None)
        if not isinstance(outputs, (list, tuple)) or len(outputs) != 2:
            raise ValueError(f"{field}: hires_dial_encoder must define two output bindings")
        record[MODE_OFFSET] = MODE_ENCODER
        for direction, output in enumerate(outputs):
            controller = getattr(output, "controller", output)
            if controller is None:
                raise ValueError(f"{field}: encoder output {direction} has no Behavior")
            identity = context.stable_behavior_id(controller, field)
            record_offset = 2 + direction * DIRECTION_SIZE
            record[record_offset] = 2
            parameter = param1 if direction == 0 else param2
            _write_binding(record, record_offset + 1, (identity, parameter, 0))
        return record

    record[MODE_OFFSET] = MODE_SCROLL_BINDING
    _write_binding(record, 2,
                   (context.stable_behavior_id(behavior, field), param1, param2))
    return record


def contribute(context):
    value = context.node_value
    sources = [node for node in context.nodes
               if node.status == "okay" and "sekigon,hires-dial-source" in set(node.compats)]
    if not sources:
        return []
    if len(sources) > 1:
        raise ValueError("only one enabled hires dial source is supported")

    encoder_sources = [node for node in context.nodes
                       if node.status == "okay" and
                       "sekigon,dynamic-encoder-source" in set(node.compats)]
    if any(value(source, "sensor-index") == 0 for source in encoder_sources):
        raise ValueError("hires dial source sensor-index 0 conflicts with a dynamic encoder source")

    keymaps = [node for node in context.nodes
               if node.status == "okay" and "zmk,keymap" in set(node.compats)]
    if len(keymaps) != 1:
        raise ValueError("hires dial sources need exactly one zmk,keymap")
    layers = list(keymaps[0].children.values())
    if not context.layer_reordering:
        layers = [layer for layer in layers if layer.status == "okay"]
    if not layers:
        raise ValueError("hires dial sources need at least one enabled keymap layer")

    groups = []
    for source in sources:
        name = context.check_name(value(source, "source-name"), "source-name")
        sensor_index = value(source, "sensor-index")
        if sensor_index != 0:
            raise ValueError(f"hires dial source {name}: sensor-index must be 0")
        input_unit = value(source, "input-unit")
        target_role = value(source, "target-role")
        label = value(source, "label")
        labels = getattr(source, "labels", [])
        if not labels:
            raise ValueError(f"hires dial source {name}: source needs a devicetree label")

        for layer_id, layer in enumerate(layers):
            bindings = value(layer, "sensor-bindings", []) or []
            if not isinstance(bindings, list):
                raise ValueError(
                    f"layer {layer_id}: sensor-bindings must be a Behavior phandle-array")
            binding = bindings[sensor_index] if sensor_index < len(bindings) else None
            record = _record(context, binding, f"layer {layer_id} hires dial")
            instance = f"{name}-i{sensor_index}-{input_unit}-l{layer_id}"
            groups.append(context.make_group({
                "module-name": "hires-dial", "group-name": "binding", "instance-name": instance,
                "label": f"{label} layer {layer_id}", "scope": "local",
                "format-version": 1, "target-role": target_role, "actual-device": source,
                "record-type": "hires-dial-binding-v1", "record-max-length": RECORD_SIZE,
                "record-default": record, "record-on-change": "hires_dial_dynamic_record_changed",
            }))
    return groups
